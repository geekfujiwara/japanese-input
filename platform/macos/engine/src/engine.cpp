#include "astelio/mac/engine.h"

#include "astelio/utf.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace astelio::mac {
namespace {

constexpr std::uintmax_t kMaxLearningBytes = 4 * 1024 * 1024;
constexpr std::uintmax_t kMaxUserDictionaryBytes = 8 * 1024 * 1024;
constexpr std::uintmax_t kMaxRecentEmojiBytes = 8 * 1024;
constexpr std::size_t kMaxRecentEmoji = 32;

} // namespace

MappedFile::MappedFile(const std::filesystem::path& path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    struct stat info {};
    if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 && info.st_size <= UINT32_MAX) {
        void* data = mmap(nullptr, static_cast<std::size_t>(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (data != MAP_FAILED) {
            data_ = data;
            size_ = static_cast<std::size_t>(info.st_size);
        }
    }
    close(fd);
}

MappedFile::~MappedFile()
{
    if (data_ != nullptr) {
        munmap(data_, size_);
    }
}

Engine::Engine(const std::filesystem::path& dictionary_path, std::filesystem::path data_directory)
    : data_directory_(std::move(data_directory))
{
    if (!dictionary_path.empty()) {
        mapped_ = std::make_unique<MappedFile>(dictionary_path);
        if (!mapped_->bytes().empty()) {
            dictionary_ = SystemDictionary::Open(mapped_->bytes());
        }
    }
    RefreshLearning();
    RefreshUserDictionary();
}

// The dictionary and the catalog point into the mapping, so they go first.
Engine::~Engine()
{
    emoji_.reset();
    dictionary_.reset();
    mapped_.reset();
}

const EmojiCatalog* Engine::EmojiCatalogForDictionary()
{
    if (!emoji_ && dictionary_) {
        emoji_ = EmojiCatalog::FromDictionary(*dictionary_);
    }
    return emoji_ ? &*emoji_ : nullptr;
}

std::optional<std::filesystem::file_time_type> Engine::Stamp(const std::filesystem::path& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    return time;
}

void Engine::RefreshLearning()
{
    const auto stamp = Stamp(LearningPath());
    if (learning_loaded_ && stamp == learning_stamp_) {
        return;
    }
    learning_loaded_ = true;
    learning_stamp_ = stamp;
    const std::optional<std::string> text = ReadFile(LearningPath(), kMaxLearningBytes);
    learning_ = text ? LearningHistory::Parse(*text) : LearningHistory();
}

bool Engine::SaveLearning()
{
    const bool saved = WriteFileAtomically(LearningPath(), learning_.Serialize());
    learning_stamp_ = Stamp(LearningPath());
    return saved;
}

bool Engine::ClearLearning()
{
    learning_.Clear();
    return SaveLearning();
}

bool Engine::RefreshUserDictionary()
{
    const auto stamp = Stamp(UserDictionaryPath());
    if (user_words_loaded_ && stamp == user_words_stamp_) {
        return false;
    }
    user_words_loaded_ = true;
    user_words_stamp_ = stamp;
    const std::optional<std::string> text = ReadFile(UserDictionaryPath(), kMaxUserDictionaryBytes);
    user_words_ = text ? UserDictionary::Parse(*text) : UserDictionary();
    return true;
}

std::vector<std::u16string> Engine::LoadRecentEmoji() const
{
    std::vector<std::u16string> recent;
    const std::optional<std::string> text = ReadFile(RecentEmojiPath(), kMaxRecentEmojiBytes);
    if (!text) {
        return recent;
    }
    std::size_t start = 0;
    while (start < text->size() && recent.size() < kMaxRecentEmoji) {
        std::size_t end = text->find('\n', start);
        if (end == std::string::npos) {
            end = text->size();
        }
        std::string_view line(text->data() + start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (std::optional<std::u16string> emoji = Utf8ToUtf16(line); emoji && !emoji->empty() && emoji->size() <= 16) {
            recent.push_back(std::move(*emoji));
        }
        start = end + 1;
    }
    return recent;
}

bool Engine::SaveRecentEmoji(const std::vector<std::u16string>& recent) const
{
    std::string text;
    for (std::size_t i = 0; i < recent.size() && i < kMaxRecentEmoji; ++i) {
        text += Utf16ToUtf8(recent[i]);
        text += '\n';
    }
    return WriteFileAtomically(RecentEmojiPath(), text);
}

std::optional<std::string> Engine::ReadFile(const std::filesystem::path& path, std::uintmax_t max_bytes)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > max_bytes) {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > max_bytes) {
        return std::nullopt;
    }
    return text;
}

bool Engine::WriteFileAtomically(const std::filesystem::path& path, std::string_view text)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::permissions(path.parent_path(), std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    const std::filesystem::path temporary = path.string() + ".tmp";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    const char* data = text.data();
    std::size_t left = text.size();
    while (left > 0) {
        const ssize_t written = write(fd, data, left);
        if (written <= 0) {
            ok = false;
            break;
        }
        data += written;
        left -= static_cast<std::size_t>(written);
    }
    ok = fsync(fd) == 0 && ok;
    ok = close(fd) == 0 && ok;
    if (!ok || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace astelio::mac
