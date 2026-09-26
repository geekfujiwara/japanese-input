#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// D-02 / D-08: the words the user added, and the candidates the user never wants to see (Suppressed).
// The public part is shared by the conversion, the import/export and the platform's manager window; the inline
// helpers work without user_dictionary.cpp.
class UserDictionary {
public:
    // Google Japanese Input's names, so its files and ours read the same.
    enum class PartOfSpeech : std::uint8_t {
        Noun,         // 名詞
        ProperNoun,   // 固有名詞
        PersonName,   // 人名
        PlaceName,    // 地名
        Organization, // 組織
        Abbreviation, // 短縮よみ: the reading is a shortcut for the surface
        Symbol,       // 記号
        Suppressed,   // 抑制単語: never show `surface` for `reading` (for any reading when it is empty)
    };

    struct Word {
        std::u16string reading; // hiragana; empty only for Suppressed
        std::u16string surface;
        PartOfSpeech pos = PartOfSpeech::Noun;
        std::u16string comment;

        bool operator==(const Word&) const = default;
    };

    static constexpr std::size_t kMaxWords = 10000;
    static constexpr std::size_t kMaxReadingLength = 64;
    static constexpr std::size_t kMaxSurfaceLength = 128;
    static constexpr std::size_t kMaxCommentLength = 256;

    static constexpr std::array<std::u16string_view, 8> kPartOfSpeechNames = {
        u"名詞", u"固有名詞", u"人名", u"地名", u"組織", u"短縮よみ", u"記号", u"抑制単語",
    };

    static constexpr std::u16string_view PartOfSpeechName(PartOfSpeech pos)
    {
        return kPartOfSpeechNames[static_cast<std::size_t>(pos)];
    }

    static constexpr std::optional<PartOfSpeech> PartOfSpeechFromName(std::u16string_view name)
    {
        for (std::size_t i = 0; i < kPartOfSpeechNames.size(); ++i) {
            if (kPartOfSpeechNames[i] == name) {
                return static_cast<PartOfSpeech>(i);
            }
        }
        return std::nullopt;
    }

    // Lengths within the limits, a surface, a reading unless Suppressed, and no tab, CR or LF anywhere.
    static bool Valid(const Word& word)
    {
        const auto clean = [](std::u16string_view text) {
            return text.find_first_of(u"\t\r\n") == std::u16string_view::npos;
        };
        return !word.surface.empty() && (!word.reading.empty() || word.pos == PartOfSpeech::Suppressed) &&
               word.reading.size() <= kMaxReadingLength && word.surface.size() <= kMaxSurfaceLength &&
               word.comment.size() <= kMaxCommentLength && clean(word.reading) && clean(word.surface) &&
               clean(word.comment);
    }

    // Adds `word`, or replaces the word with the same reading, surface and part of speech.
    // False when it is not Valid or kMaxWords are stored.
    bool Add(Word word);
    // Replaces `old` with `word`; false when `old` is missing or `word` is not Valid.
    bool Update(const Word& old, Word word);
    bool Remove(const Word& word);
    void Clear();

    // In the order they were added.
    const std::vector<Word>& Words() const { return words_; }
    bool Empty() const { return words_.empty(); }
    // Words whose reading, surface or comment contains `text` (all of them for an empty text), in stored order.
    std::vector<Word> Search(std::u16string_view text) const;

    // Surfaces to offer for exactly `reading` (not Suppressed), in stored order.
    std::vector<std::u16string> Lookup(std::u16string_view reading) const;
    // Surfaces of the words whose reading starts with `prefix` (not Suppressed), at most `limit`.
    std::vector<std::u16string> Predict(std::u16string_view prefix, std::size_t limit) const;
    // D-08: whether a Suppressed word hides `surface` for `reading`. An empty `reading` (not known) matches any
    // Suppressed word of `surface`.
    bool Suppressed(std::u16string_view reading, std::u16string_view surface) const;

    // UTF-8 text: the header line "# astelio user dictionary 1", then one word per line: reading, surface,
    // part of speech name and comment separated by tabs.
    std::string Serialize() const;
    // Lines that are not well formed are skipped; at most kMaxWords are kept.
    static UserDictionary Parse(std::string_view text);

private:
    std::vector<Word> words_;
};

} // namespace astelio
