#pragma once

// Binary system dictionary layout (little-endian). Shared by the reader and the builder.
//
// Header (80 bytes):
//   0  char[8] magic "ASTLDIC\0"
//   8  u32 version
//   12 u32 id_count        connection matrix is id_count x id_count
//   16 u32 bos_id
//   20 u32 eos_id
//   24 u32 reading_count
//   28 u32 readings_offset  ReadingRecord[reading_count], sorted by reading (UTF-16 code units)
//   32 u32 entry_count
//   36 u32 entries_offset   EntryRecord[entry_count], grouped by reading, cheapest first
//   40 u32 pool_offset      char16_t[pool_units]
//   44 u32 pool_units
//   48 u32 matrix_offset    i16[id_count * id_count], row = previous right id, column = next left id
//   52 u32 file_size
//   56 u32 word_types_offset u8[id_count] (WordType per part-of-speech id)
//   60 u16 unknown_id       part-of-speech id for text that is not in the dictionary
//   62 i16 unknown_cost
//   64 u32 meaning_count    0 when there is no meaning model
//   68 u32 meaning_matrix_offset i16[meaning_count * meaning_count], row = earlier segment, column = later one
//   72 u32 meaning_flags_offset  u8[id_count]: 1 when a word of this part-of-speech id gives its segment its meaning
//   76 u16 neutral_meaning  meaning id that connects to everything at cost 0
//   78 u16 reserved (0)

#include <bit>
#include <cstddef>
#include <cstdint>

namespace astelio::dictionary_format {

static_assert(std::endian::native == std::endian::little, "the dictionary format is little-endian");

inline constexpr char kMagic[8] = {'A', 'S', 'T', 'L', 'D', 'I', 'C', '\0'};
inline constexpr std::uint32_t kVersion = 3;
inline constexpr std::size_t kHeaderSize = 80;
inline constexpr std::size_t kReadingRecordSize = 12; // u32 text, u32 first_entry, u16 length, u16 entry_count
// u32 text, u16 length, u16 left, u16 right, i16 cost, u16 meaning, u16 reserved
inline constexpr std::size_t kEntryRecordSize = 16;
inline constexpr std::size_t kMaxTextLength = 255;
inline constexpr std::uint32_t kMaxMeanings = 4096;

namespace header {
inline constexpr std::size_t kVersion = 8;
inline constexpr std::size_t kIdCount = 12;
inline constexpr std::size_t kBosId = 16;
inline constexpr std::size_t kEosId = 20;
inline constexpr std::size_t kReadingCount = 24;
inline constexpr std::size_t kReadingsOffset = 28;
inline constexpr std::size_t kEntryCount = 32;
inline constexpr std::size_t kEntriesOffset = 36;
inline constexpr std::size_t kPoolOffset = 40;
inline constexpr std::size_t kPoolUnits = 44;
inline constexpr std::size_t kMatrixOffset = 48;
inline constexpr std::size_t kFileSize = 52;
inline constexpr std::size_t kWordTypesOffset = 56;
inline constexpr std::size_t kUnknownId = 60;
inline constexpr std::size_t kUnknownCost = 62;
inline constexpr std::size_t kMeaningCount = 64;
inline constexpr std::size_t kMeaningMatrixOffset = 68;
inline constexpr std::size_t kMeaningFlagsOffset = 72;
inline constexpr std::size_t kNeutralMeaning = 76;
} // namespace header

} // namespace astelio::dictionary_format
