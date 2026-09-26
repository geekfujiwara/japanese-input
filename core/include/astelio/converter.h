#pragma once

#include "astelio/dictionary.h"
#include "astelio/special_candidates.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

struct ConvertedSegment {
    std::u16string reading;
    std::vector<std::u16string> candidates; // best first; always includes the hiragana and katakana forms
    // The head of the segment (up to the last content word) can be replaced; the particles and endings after it
    // (`tail`, as written in the best candidate) are kept. 0: the whole reading is the head.
    std::size_t head_length = 0;
    std::u16string tail;
    // The right id of the last word of candidates.front(), the context for what is typed next.
    std::uint16_t right_id = 0;
};

// Kana-kanji conversion: minimum-cost path over the dictionary lattice (word costs + connection costs),
// then words grouped into segments (bunsetsu).
class Converter {
public:
    // `dictionary` must outlive the converter.
    explicit Converter(const SystemDictionary& dictionary) : dictionary_(dictionary) {}

    // `fixed_lengths`: reading lengths of leading segments the user has resized; they are kept as segments
    // and the rest is segmented freely. `previous_right_id`: the right id of the word committed just before
    // (its connection cost decides the first word); the beginning of a sentence when there is none.
    std::vector<ConvertedSegment> Convert(std::u16string_view reading,
                                          std::span<const std::size_t> fixed_lengths = {},
                                          std::optional<std::uint16_t> previous_right_id = std::nullopt) const;

    // Predictive candidates while typing: the conversion of `reading`, then words whose reading starts with it.
    std::vector<std::u16string> Predict(std::u16string_view reading, std::size_t limit) const;

    // The clock for date and time candidates (tests fix it); the system clock by default.
    void SetClock(std::function<LocalTime()> clock) { clock_ = std::move(clock); }

    const SystemDictionary& dictionary() const { return dictionary_; }

private:
    const SystemDictionary& dictionary_;
    std::function<LocalTime()> clock_;
};

std::u16string HiraganaToKatakana(std::u16string_view text);

// Likely typos fixed: a doubled っ, ん, ー or small kana is typed once ("たっっせい" -> "たっせい").
// `origin[i]` is the index in the original text of `text[i]`; origin.back() is the original length.
struct TypoCorrection {
    std::u16string text;
    std::vector<std::size_t> origin;
};
TypoCorrection CorrectTypos(std::u16string_view text);

} // namespace astelio
