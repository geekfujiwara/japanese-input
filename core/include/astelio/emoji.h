#pragma once

#include "astelio/dictionary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace astelio {

enum class EmojiCategory : std::uint8_t {
    Recent,
    Smileys,
    People,
    Nature,
    Food,
    Activities,
    Travel,
    Objects,
    Symbols,
    Flags,
};
inline constexpr std::size_t kEmojiCategoryCount = 10;

struct Emoji {
    std::u16string text;
    std::vector<std::u16string> readings; // hiragana, for search
    EmojiCategory category = EmojiCategory::Symbols;
};

// True when `text` is one emoji (code points, modifiers and joiners only).
bool IsEmoji(std::u16string_view text);
EmojiCategory CategorizeEmoji(std::u16string_view text);

// B-13: every emoji in the dictionary, grouped by category, searchable by reading.
class EmojiCatalog {
public:
    static EmojiCatalog FromDictionary(const SystemDictionary& dictionary);

    void Add(std::u16string_view text, std::u16string_view reading);
    // Sorts each category by code point; call after the last Add.
    void Finish();

    std::size_t size() const { return emoji_.size(); }
    const Emoji& at(std::size_t index) const { return emoji_.at(index); }
    const std::vector<std::size_t>& InCategory(EmojiCategory category) const;
    // Emoji whose reading starts with `query` first, then those containing it.
    std::vector<std::size_t> Search(std::u16string_view query, std::size_t limit) const;

private:
    std::vector<Emoji> emoji_;
    std::unordered_map<std::u16string, std::size_t> index_;
    std::array<std::vector<std::size_t>, kEmojiCategoryCount> categories_;
};

} // namespace astelio
