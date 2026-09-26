#include "learning_store.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace astelio::tip {
namespace {

constexpr DWORD kMaxFileBytes = 4 * 1024 * 1024;
constexpr wchar_t kDefaultSettingsKey[] = L"Software\\AstelioIME";
constexpr wchar_t kEnabledValue[] = L"LearningEnabled";
constexpr wchar_t kPausedValue[] = L"LearningPaused";
constexpr wchar_t kExcludedAppsValue[] = L"NoLearningApps";

std::mutex g_mutex;
std::wstring g_override;
std::wstring g_settings_key = kDefaultSettingsKey;

std::wstring SettingsKey()
{
    const std::lock_guard lock(g_mutex);
    return g_settings_key;
}

// A DWORD setting under HKCU\Software\AstelioIME; `fallback` when missing.
bool ReadFlag(const wchar_t* name, bool fallback = true)
{
    DWORD value = fallback ? 1 : 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey().c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    return value != 0;
}

void WriteFlag(const wchar_t* name, bool on)
{
    const DWORD value = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, SettingsKey().c_str(), name, REG_DWORD, &value, sizeof(value));
}

std::vector<std::wstring> ExcludedApps()
{
    const std::wstring key = SettingsKey();
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kExcludedAppsValue, RRF_RT_REG_MULTI_SZ, nullptr, nullptr,
                     &bytes) != ERROR_SUCCESS ||
        bytes == 0 || bytes > 64 * 1024) {
        return {};
    }
    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kExcludedAppsValue, RRF_RT_REG_MULTI_SZ, nullptr,
                     buffer.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    std::vector<std::wstring> apps;
    for (const wchar_t* name = buffer.c_str(); *name != L'\0'; name += wcslen(name) + 1) {
        apps.emplace_back(name);
    }
    return apps;
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

void UseSettingsKey(const wchar_t* key)
{
    const std::lock_guard lock(g_mutex);
    g_settings_key = key != nullptr ? key : kDefaultSettingsKey;
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
    WriteFlag(kEnabledValue, enabled);
}

bool LearningPaused()
{
    return ReadFlag(kPausedValue, false);
}

void SetLearningPaused(bool paused)
{
    WriteFlag(kPausedValue, paused);
}

bool AppLearningExcluded(const std::wstring& app)
{
    const std::vector<std::wstring> apps = ExcludedApps();
    return !app.empty() && std::find(apps.begin(), apps.end(), app) != apps.end();
}

void SetAppLearningExcluded(const std::wstring& app, bool excluded)
{
    if (app.empty()) {
        return;
    }
    std::vector<std::wstring> apps = ExcludedApps();
    std::erase(apps, app);
    if (excluded) {
        apps.push_back(app);
    }
    const std::wstring key = SettingsKey();
    if (apps.empty()) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), kExcludedAppsValue);
        return;
    }
    std::wstring list;
    for (const std::wstring& name : apps) {
        list += name;
        list += L'\0';
    }
    list += L'\0';
    RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), kExcludedAppsValue, REG_MULTI_SZ, list.data(),
                    static_cast<DWORD>(list.size() * sizeof(wchar_t)));
}

} // namespace astelio::tip
