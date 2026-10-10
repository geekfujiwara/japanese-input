#pragma once

#include "astelio/converter.h"
#include "astelio/dictionary.h"
#include "astelio/emoji.h"
#include "astelio/learning.h"
#include "astelio/user_dictionary.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace astelio::mac {

// A read-only memory map of a file (the system dictionary). Empty when the file is missing or too large.
class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    std::span<const std::byte> bytes() const { return {static_cast<const std::byte*>(data_), size_}; }

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

// What the input method shares between the text fields of every app: the system dictionary (mapped from the
// bundle), the user's history, words and recent emoji. The files are kept in `data_directory`
// (~/Library/Application Support/AstelioIME, or the same path inside the App Sandbox container), written as a whole
// through a temporary file and readable by this user only. Used from the main thread only.
class Engine {
public:
    Engine(const std::filesystem::path& dictionary_path, std::filesystem::path data_directory);
    ~Engine();

    // nullptr when the dictionary is missing or broken (typing kana still works).
    const SystemDictionary* dictionary() const { return dictionary_ ? &*dictionary_ : nullptr; }
    // B-13: built on first use.
    const EmojiCatalog* EmojiCatalogForDictionary();

    const std::filesystem::path& data_directory() const { return data_directory_; }
    std::filesystem::path LearningPath() const { return data_directory_ / "learning.tsv"; }
    std::filesystem::path UserDictionaryPath() const { return data_directory_ / "user_dictionary.tsv"; }
    std::filesystem::path RecentEmojiPath() const { return data_directory_ / "emoji_recent.txt"; }

    // D-04: the history. Reloaded when the file changed since it was read (another tool edited it).
    LearningHistory& learning() { return learning_; }
    void RefreshLearning();
    bool SaveLearning();
    bool ClearLearning();

    // D-02 / D-08: nullptr while the dictionary has no words. Reloaded when the file changed.
    const UserDictionary* user_dictionary() const { return user_words_.Empty() ? nullptr : &user_words_; }
    // Returns whether the words changed.
    bool RefreshUserDictionary();

    // B-13: most recent first.
    std::vector<std::u16string> LoadRecentEmoji() const;
    bool SaveRecentEmoji(const std::vector<std::u16string>& recent) const;

    // Writes `text` to `path` through a temporary file in the same folder, readable by this user only.
    static bool WriteFileAtomically(const std::filesystem::path& path, std::string_view text);
    // The file as text, or std::nullopt when it is missing or larger than `max_bytes`.
    static std::optional<std::string> ReadFile(const std::filesystem::path& path, std::uintmax_t max_bytes);

private:
    static std::optional<std::filesystem::file_time_type> Stamp(const std::filesystem::path& path);

    std::unique_ptr<MappedFile> mapped_;
    std::optional<SystemDictionary> dictionary_;
    std::optional<EmojiCatalog> emoji_;
    std::filesystem::path data_directory_;
    LearningHistory learning_;
    std::optional<std::filesystem::file_time_type> learning_stamp_;
    bool learning_loaded_ = false;
    UserDictionary user_words_;
    std::optional<std::filesystem::file_time_type> user_words_stamp_;
    bool user_words_loaded_ = false;
};

} // namespace astelio::mac
