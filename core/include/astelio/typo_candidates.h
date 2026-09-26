#pragma once

#include "astelio/dictionary.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// B-14: a dictionary word the user may have meant when one key slipped.
struct TypoCandidate {
    std::u16string keys;    // the corrected keys
    std::u16string reading; // their kana
    std::u16string surface;
    std::int32_t cost = 0;  // word cost + kTypoKeyPenalty
};

inline constexpr std::int32_t kTypoKeyPenalty = 300;

// Words whose whole reading is reached from `keys` by one missing, extra, neighbouring (US layout) or swapped key,
// cheapest first, one per surface. The reading typed as is never appears.
std::vector<TypoCandidate> FindTypoCandidates(std::u16string_view keys, const RomajiTable& table,
                                              const SystemDictionary& dictionary, std::size_t limit);

// Keys next to `key` on a US keyboard (letters and '-').
std::u16string_view NeighbouringKeys(char16_t key);

} // namespace astelio
