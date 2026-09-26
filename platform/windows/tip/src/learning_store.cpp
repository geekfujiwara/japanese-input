#include "learning_store.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace astelio::tip {
namespace {

constexpr DWORD kMaxFileBytes = 4 * 1024 * 1024;
constexpr wchar_t kSettingsKey[] = L"Software\\AstelioIME";
constexpr wchar_t kEnabledValue[] = L"LearningEnabled";

std::mutex g_mutex;
std::wstring g_override;

// A DWORD setting under HKCU\Software\AstelioIME; on when missing.
bool ReadFlag(const wchar_t* name)
{
    DWORD value = 1;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) !=
        ERROR_SUCCESS) {
        return true;
    }
    return value != 0;
}

std::wstring LearningPath()
{
    const std::lock_guard lock(g_mutex);
    if (!g_override.empty()) {
        return g_override;
    }
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(buffer, length) + L"\\AstelioIME\\learning.tsv";
}

} // namespace

void UseLearningFile(const wchar_t* path)
{
    const std::lock_guard lock(g_mutex);
    g_override = path != nullptr ? path : L"";
}

LearningHistory LoadLearning()
{
    try {
        const std::wstring path = LearningPath();
        if (path.empty()) {
            return {};
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return {};
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > kMaxFileBytes) {
            CloseHandle(file);
            return {};
        }
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        CloseHandle(file);
        if (!ok) {
            return {};
        }
        bytes.resize(read);
        return LearningHistory::Parse(bytes);
    } catch (...) {
        return {};
    }
}

void SaveLearning(const LearningHistory& history)
{
    try {
        const std::wstring path = LearningPath();
        if (path.empty()) {
            return;
        }
        const std::size_t slash = path.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        }
        const std::string text = history.Serialize();
        // Write a temporary file and swap it in, so other apps never read half a file. Deleted words leave no
        // trace, since the whole file is rewritten (T-D05-3).
        const std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                        FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok || written != text.size() ||
            !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
        }
    } catch (...) {
        // The history is a convenience; typing goes on without it.
    }
}

std::uint64_t LearningFileStamp()
{
    try {
        const std::wstring path = LearningPath();
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
            return 0;
        }
        return (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
               data.ftLastWriteTime.dwLowDateTime;
    } catch (...) {
        return 0;
    }
}

bool LearningEnabled()
{
    return ReadFlag(kEnabledValue);
}

bool TypoSuggestionsEnabled()
{
    return ReadFlag(L"TypoSuggestions");
}

void SetLearningEnabled(bool enabled)
{
    const DWORD value = enabled ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kEnabledValue, REG_DWORD, &value, sizeof(value));
}

} // namespace astelio::tip
