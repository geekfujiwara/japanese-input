#include "user_dictionary_store.h"

#include <windows.h>

#include <climits>
#include <mutex>
#include <utility>

namespace astelio::tip {
namespace {

constexpr DWORD kMaxFileBytes = 16 * 1024 * 1024;
constexpr UINT kShiftJisCodePage = 932;

std::mutex g_mutex;
std::wstring g_override;

std::wstring UserDictionaryPath()
{
    const std::lock_guard lock(g_mutex);
    if (!g_override.empty()) {
        return g_override;
    }
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(buffer, length) + L"\\AstelioIME\\user_dictionary.tsv";
}

std::optional<std::string> ReadWholeFile(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > kMaxFileBytes) {
        CloseHandle(file);
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        return std::nullopt;
    }
    bytes.resize(read);
    return bytes;
}

// Writes a temporary file and swaps it in, so other apps never read half a file.
bool WriteWholeFile(const std::wstring& path, std::string_view bytes)
{
    const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || written != bytes.size() ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

void UseUserDictionaryFile(const wchar_t* path)
{
    const std::lock_guard lock(g_mutex);
    g_override = path != nullptr ? path : L"";
}

UserDictionary LoadUserDictionary()
{
    try {
        const std::wstring path = UserDictionaryPath();
        const std::optional<std::string> bytes = path.empty() ? std::nullopt : ReadWholeFile(path);
        return bytes ? UserDictionary::Parse(*bytes) : UserDictionary();
    } catch (...) {
        return {};
    }
}

bool SaveUserDictionary(const UserDictionary& dictionary)
{
    try {
        const std::wstring path = UserDictionaryPath();
        if (path.empty()) {
            return false;
        }
        const std::size_t slash = path.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        }
        return WriteWholeFile(path, dictionary.Serialize());
    } catch (...) {
        return false;
    }
}

std::uint64_t UserDictionaryFileStamp()
{
    try {
        const std::wstring path = UserDictionaryPath();
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
            return 0;
        }
        const std::uint64_t time = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                                   data.ftLastWriteTime.dwLowDateTime;
        // The size too, since two writes within one clock tick keep the same time.
        const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        return time ^ (size * 0x9E3779B97F4A7C15ull);
    } catch (...) {
        return 0;
    }
}

std::optional<std::u16string> DecodeUserDictionaryFile(std::string_view bytes)
{
    if (DetectEncoding(bytes) != TextEncoding::ShiftJis) {
        return DecodeText(bytes);
    }
    if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int size = static_cast<int>(bytes.size());
    const int length = MultiByteToWideChar(kShiftJisCodePage, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
    if (length <= 0) {
        return std::nullopt;
    }
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    if (MultiByteToWideChar(kShiftJisCodePage, MB_ERR_INVALID_CHARS, bytes.data(), size,
                            reinterpret_cast<wchar_t*>(text.data()), length) != length) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> EncodeUserDictionaryFile(std::u16string_view text, TextEncoding encoding)
{
    if (encoding != TextEncoding::ShiftJis) {
        return EncodeText(text, encoding);
    }
    if (text.empty()) {
        return std::string();
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const auto* wide = reinterpret_cast<const wchar_t*>(text.data());
    const int size = static_cast<int>(text.size());
    BOOL lost = FALSE;
    const int length =
        WideCharToMultiByte(kShiftJisCodePage, WC_NO_BEST_FIT_CHARS, wide, size, nullptr, 0, nullptr, &lost);
    if (length <= 0 || lost) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(kShiftJisCodePage, WC_NO_BEST_FIT_CHARS, wide, size, bytes.data(), length, nullptr,
                            &lost) != length ||
        lost) {
        return std::nullopt;
    }
    return bytes;
}

UserDictionaryFileImport ImportUserDictionaryFile(const std::wstring& path, UserDictionary& dictionary)
{
    UserDictionaryFileImport result;
    try {
        const std::optional<std::string> bytes = ReadWholeFile(path);
        const std::optional<std::u16string> text = bytes ? DecodeUserDictionaryFile(*bytes) : std::nullopt;
        if (!text) {
            return result;
        }
        result.read = true;
        UserDictionaryImport imported = ImportUserDictionary(*text);
        result.format = imported.format;
        if (!imported.format) {
            return result;
        }
        result.skipped = imported.skipped;
        for (UserDictionary::Word& word : imported.words) {
            if (dictionary.Add(std::move(word))) {
                ++result.added;
            } else {
                ++result.skipped;
            }
        }
    } catch (...) {
        result.read = false;
    }
    return result;
}

UserDictionaryExport ExportUserDictionaryFile(const std::wstring& path, const std::vector<UserDictionary::Word>& words,
                                              UserDictionaryFormat format)
{
    try {
        const std::optional<std::string> bytes =
            EncodeUserDictionaryFile(ExportUserDictionary(words, format), ExportEncoding(format));
        if (!bytes) {
            return UserDictionaryExport::NotEncodable;
        }
        return WriteWholeFile(path, *bytes) ? UserDictionaryExport::Written : UserDictionaryExport::NotWritten;
    } catch (...) {
        return UserDictionaryExport::NotWritten;
    }
}

} // namespace astelio::tip
