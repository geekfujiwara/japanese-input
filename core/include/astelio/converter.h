#pragma once

#include "astelio/dictionary.h"

#include <cstddef>
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

private:
    const SystemDictionary& dictionary_;
};

std::u16string HiraganaToKatakana(std::u16string_view text);

} // namespace astelio
