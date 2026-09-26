#pragma once

#include "astelio/dictionary.h"
#include "astelio/romaji_table.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// B-14: a dictionary word the user may have meant when one key slipped.
struct TypoCandidate {
    std::u16string keys;    // the corrected keys
    std::u16string reading; // their kana
    std::u16string surface;
    std::int32_t cost = 0;  // word cost + the penalty of the edit
    std::uint16_t left_id = 0;
    std::uint16_t right_id = 0;
};

// One missing, extra, neighbouring (US layout) or swapped key.
inline constexpr std::int32_t kTypoKeyPenalty = 300;
// A vowel typed as another vowel, a key typed for a similar romaji (h/f, j/z, v/b), or both keys of a
// doubled consonant slipped the same way ("nerro" for "netto").
inline constexpr std::int32_t kTypoSoundPenalty = 400;
// The most common slips: a vowel or ー pressed twice, and a long vowel left out ("ryoko" for "ryokou").
inline constexpr std::int32_t kTypoLikelyPenalty = 150;
// Added when the edit needs a key few people type in romaji (c other than ch, q, x, l).
inline constexpr std::int32_t kTypoRareKeySurcharge = 200;
// How much more likely than the word typed a candidate must be (in cost) to be suggested. Chosen with the margin
// table of astelio_typo: 500 keeps both false suggestion rates at 0 (2026-09-26).
inline constexpr std::int32_t kTypoSuggestMargin = 500;

// Words (content words only) whose whole reading is reached from `keys` by one of the edits above, cheapest
// first, one per surface. The reading typed as is never appears.
std::vector<TypoCandidate> FindTypoCandidates(std::u16string_view keys, const RomajiTable& table,
                                              const SystemDictionary& dictionary, std::size_t limit);

// The もしかして suggestion for `keys`, or nothing. `previous_right_id` is the right id of the word before it
// (the beginning of the sentence when there is none). A candidate is suggested when the reading typed is not a
// word, or when the candidate, with the connection from the word before, is more likely than the word typed by
// `margin`.
std::optional<TypoCandidate> SuggestTypoCorrection(std::u16string_view keys, const RomajiTable& table,
                                                   const SystemDictionary& dictionary,
                                                   std::optional<std::uint16_t> previous_right_id = std::nullopt,
                                                   std::int32_t margin = kTypoSuggestMargin);

// Keys next to `key` on a US keyboard (letters and '-').
std::u16string_view NeighbouringKeys(char16_t key);

} // namespace astelio
