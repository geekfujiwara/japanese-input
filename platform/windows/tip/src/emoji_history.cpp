#include "emoji_history.h"

#include "astelio/emoji.h"
#include "astelio/input_session.h"
#include "astelio/utf.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace astelio::tip {
namespace {

constexpr DWORD kMaxFileBytes = 8 * 1024;

std::mutex g_mutex;
std::wstring g_override;

std::wstring DefaultDirectory()
{
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(buffer, length) + L"\\AstelioIME";
}

std::wstring HistoryPath()
{
    const std::lock_guard lock(g_mutex);
    if (!g_override.empty()) {
        return g_override;
    }
    const std::wstring directory = DefaultDirectory();
    return directory.empty() ? std::wstring() : directory + L"\\emoji_recent.txt";
}

} // namespace

void UseRecentEmojiFile(const wchar_t* path)
{
    const std::lock_guard lock(g_mutex);
    g_override = path != nullptr ? path : L"";
}

std::vector<std::u16string> LoadRecentEmoji()
{
    std::vector<std::u16string> recent;
    try {
        const std::wstring path = HistoryPath();
        if (path.empty()) {
            return recent;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return recent;
        }
        std::string bytes(kMaxFileBytes, '\0');
        DWORD read = 0;
        const BOOL ok = ReadFile(file, bytes.data(), kMaxFileBytes, &read, nullptr);
        CloseHandle(file);
        if (!ok) {
            return recent;
        }
        bytes.resize(read);
        std::size_t begin = 0;
        while (begin < bytes.size() && recent.size() < InputSession::kMaxRecentEmoji) {
            std::size_t end = bytes.find('\n', begin);
            if (end == std::string::npos) {
                end = bytes.size();
            }
            std::string_view line(bytes.data() + begin, end - begin);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            // The file is outside the IME's control: keep only well-formed single emoji.
            if (const std::optional<std::u16string> emoji = Utf8ToUtf16(line);
                emoji && IsEmoji(*emoji) && std::find(recent.begin(), recent.end(), *emoji) == recent.end()) {
                recent.push_back(*emoji);
            }
            begin = end + 1;
        }
    } catch (...) {
        recent.clear();
    }
    return recent;
}

void SaveRecentEmoji(const std::vector<std::u16string>& recent)
{
    try {
        const std::wstring path = HistoryPath();
        if (path.empty()) {
            return;
        }
        const std::size_t slash = path.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        }
        std::string text;
        for (std::size_t i = 0; i < recent.size() && i < InputSession::kMaxRecentEmoji; ++i) {
            text += Utf16ToUtf8(recent[i]);
            text += '\n';
        }
        // Write a temporary file and swap it in, so other apps never read half a file.
        const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
        if (!ok || written != text.size() ||
            !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(temporary.c_str());
        }
    } catch (...) {
        // The history is a convenience; losing it is harmless.
    }
}

} // namespace astelio::tip
