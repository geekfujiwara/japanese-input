#include "dictionary_loader.h"

#include "module.h"

#include <windows.h>

#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace astelio::tip {
namespace {

class MappedDictionary {
public:
    ~MappedDictionary()
    {
        converter_.reset();
        emoji_.reset();
        dictionary_.reset();
        if (view_ != nullptr) {
            UnmapViewOfFile(view_);
        }
        if (mapping_ != nullptr) {
            CloseHandle(mapping_);
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
        }
    }

    bool Open(const wchar_t* path)
    {
        file_ = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (file_ == INVALID_HANDLE_VALUE || !GetFileSizeEx(file_, &size) || size.QuadPart <= 0 ||
            size.QuadPart > UINT32_MAX) {
            return false;
        }
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            return false;
        }
        view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
        if (view_ == nullptr) {
            return false;
        }
        const std::span<const std::byte> bytes(static_cast<const std::byte*>(view_),
                                               static_cast<std::size_t>(size.QuadPart));
        dictionary_ = SystemDictionary::Open(bytes);
        if (!dictionary_) {
            return false;
        }
        converter_.emplace(*dictionary_);
        return true;
    }

    const Converter* converter() const { return converter_ ? &*converter_ : nullptr; }

    const EmojiCatalog* emoji()
    {
        if (!emoji_ && dictionary_) {
            emoji_ = EmojiCatalog::FromDictionary(*dictionary_);
        }
        return emoji_ ? &*emoji_ : nullptr;
    }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
    std::optional<SystemDictionary> dictionary_;
    std::optional<Converter> converter_;
    std::optional<EmojiCatalog> emoji_;
};

std::mutex g_mutex;
bool g_tried = false;
const Converter* g_current = nullptr;
// Replaced dictionaries stay mapped: sessions may still point at their converters.
std::vector<std::unique_ptr<MappedDictionary>> g_dictionaries;

std::wstring InstalledDictionaryPath()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while (true) {
        length = GetModuleFileNameW(ModuleHandle(), path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            break;
        }
        path.resize(path.size() * 2);
    }
    path.resize(length);
    // <install dir>\<arch>\astelio_tip.dll -> <install dir>\dictionary\system.dic
    for (int level = 0; level < 2; ++level) {
        const std::size_t slash = path.find_last_of(L'\\');
        if (slash == std::wstring::npos) {
            return {};
        }
        path.resize(slash);
    }
    return path + L"\\dictionary\\system.dic";
}

const Converter* Load(const wchar_t* path)
{
    auto dictionary = std::unique_ptr<MappedDictionary>(new (std::nothrow) MappedDictionary());
    if (!dictionary || !dictionary->Open(path)) {
        return nullptr;
    }
    const Converter* converter = dictionary->converter();
    g_dictionaries.push_back(std::move(dictionary));
    return converter;
}

} // namespace

const Converter* SharedConverter()
{
    try {
        const std::lock_guard lock(g_mutex);
        if (!g_tried) {
            g_tried = true;
            const std::wstring path = InstalledDictionaryPath();
            if (!path.empty()) {
                g_current = Load(path.c_str());
            }
        }
        return g_current;
    } catch (...) {
        return nullptr;
    }
}

const Converter* UseDictionaryFile(const wchar_t* path)
{
    try {
        const std::lock_guard lock(g_mutex);
        g_tried = true;
        g_current = path != nullptr ? Load(path) : nullptr;
        return g_current;
    } catch (...) {
        return nullptr;
    }
}

const EmojiCatalog* EmojiCatalogFor(const Converter* converter)
{
    if (converter == nullptr) {
        return nullptr;
    }
    try {
        const std::lock_guard lock(g_mutex);
        for (const std::unique_ptr<MappedDictionary>& dictionary : g_dictionaries) {
            if (dictionary->converter() == converter) {
                return dictionary->emoji();
            }
        }
    } catch (...) {
        // Out of memory: no palette.
    }
    return nullptr;
}

void ReleaseDictionaries()
{
    const std::lock_guard lock(g_mutex);
    g_current = nullptr;
    g_tried = false;
    g_dictionaries.clear();
}

} // namespace astelio::tip
