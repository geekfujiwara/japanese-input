#pragma once

#include "astelio/dictionary.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Reads the binary dictionary of azooKey_dictionary_storage (Apache-2.0).
// Layout per AzooKeyKanaKanjiConverter (MIT): louds/*.loudstxt3 hold the entries of each trie node,
// cb/<right id>.binary hold one row of the part-of-speech connection matrix.
namespace astelio::azookey {

inline constexpr std::uint16_t kIdCount = 1319;
inline constexpr std::uint16_t kBosId = 0;
inline constexpr std::uint16_t kEosId = 1316;
// azooKey's value for a connection row that has no file.
inline constexpr float kMissingRowValue = -25.0f;
// Text not in the dictionary is treated as a proper noun, about as likely as the rarest dictionary words.
inline constexpr std::uint16_t kUnknownId = 1288;
inline constexpr std::int16_t kUnknownCost = 3000;
// Meaning ids (mid); 500 is BOS/EOS and connects to everything at 0.
inline constexpr std::uint16_t kMeaningCount = 502;
inline constexpr std::uint16_t kNeutralMeaning = 500;

// Segment role of an azooKey part-of-speech id (same classes as azooKey's clause detection).
WordType WordTypeOf(std::uint16_t id);
// Non-content ids whose words still set their segment's meaning (dependent verbs and nouns).
bool GivesMeaning(std::uint16_t id);

struct Entry {
    std::u16string reading; // katakana, as stored by azooKey
    std::u16string surface;
    std::uint16_t left_id = 0;
    std::uint16_t right_id = 0;
    std::uint16_t meaning_id = 0;
    float value = 0; // log probability: larger is more likely
};

// .loudstxt3: u16 record count, u32 offsets[count], then records of
// u16 n, n x (u16 left, u16 right, u16 meaning, f32 value), UTF-8 "ruby\tword1\t...\twordN" (empty word = ruby).
// Returns false when the file is malformed.
bool ParseLoudsText(std::span<const std::byte> bytes, std::vector<Entry>& out);

// cb/<id>.binary: pairs of (i32 left id, f32 value); the first pair has id -1 and holds the row default.
std::optional<std::vector<std::pair<std::int32_t, float>>> ParseConnectionRow(std::span<const std::byte> bytes);

// mm.binary: f32[kMeaningCount * kMeaningCount] log probabilities, row = the earlier segment's meaning.
std::optional<std::vector<float>> ParseMeaningMatrix(std::span<const std::byte> bytes);

std::u16string KatakanaToHiragana(std::u16string_view text);

// Astelio costs are integers where lower is more likely.
std::int16_t ToCost(float value);

} // namespace astelio::azookey
