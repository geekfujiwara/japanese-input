#pragma once

#include "astelio/dictionary.h"
#include "astelio/special_candidates.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

struct ConvertedSegment {
    std::u16string reading;
    std::vector<std::u16string> candidates; // best first; always includes the hiragana and katakana forms
};

// Kana-kanji conversion: minimum-cost path over the dictionary lattice (word costs + connection costs),
// then words grouped into segments (bunsetsu).
class Converter {
public:
    // `dictionary` must outlive the converter.
    explicit Converter(const SystemDictionary& dictionary) : dictionary_(dictionary) {}

    // `fixed_lengths`: reading lengths of leading segments the user has resized; they are kept as segments
    // and the rest is segmented freely.
    std::vector<ConvertedSegment> Convert(std::u16string_view reading,
                                          std::span<const std::size_t> fixed_lengths = {}) const;

    // Predictive candidates while typing: the conversion of `reading`, then words whose reading starts with it.
    std::vector<std::u16string> Predict(std::u16string_view reading, std::size_t limit) const;

    // The clock for date and time candidates (tests fix it); the system clock by default.
    void SetClock(std::function<LocalTime()> clock) { clock_ = std::move(clock); }

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
