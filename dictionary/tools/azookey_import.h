#pragma once

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

std::u16string KatakanaToHiragana(std::u16string_view text);

// Astelio costs are integers where lower is more likely.
std::int16_t ToCost(float value);

} // namespace astelio::azookey
