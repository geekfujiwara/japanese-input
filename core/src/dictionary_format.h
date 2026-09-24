#pragma once

// Binary system dictionary layout (little-endian). Shared by the reader and the builder.
//
// Header (64 bytes):
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
//   56 reserved (zero)

#include <bit>
#include <cstddef>
#include <cstdint>

namespace astelio::dictionary_format {

static_assert(std::endian::native == std::endian::little, "the dictionary format is little-endian");

inline constexpr char kMagic[8] = {'A', 'S', 'T', 'L', 'D', 'I', 'C', '\0'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 64;
inline constexpr std::size_t kReadingRecordSize = 12; // u32 text, u32 first_entry, u16 length, u16 entry_count
inline constexpr std::size_t kEntryRecordSize = 12;   // u32 text, u16 length, u16 left, u16 right, i16 cost
inline constexpr std::size_t kMaxTextLength = 255;

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
} // namespace header

} // namespace astelio::dictionary_format
