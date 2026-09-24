#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace astelio {

struct DictionaryEntry {
    std::u16string_view surface;
    std::uint16_t left_id = 0;
    std::uint16_t right_id = 0;
    std::int16_t cost = 0; // lower is more likely
};

// Role of a part-of-speech id in a segment (bunsetsu): a segment is prefixes, content words, then suffixes
// (particles, auxiliary verbs, inflections).
enum class WordType : std::uint8_t {
    Prefix = 0,
    Content = 1,
    Suffix = 2,
    Edge = 3, // beginning / end of sentence
};

enum class DictionaryError : std::uint8_t {
    TooSmall,
    Misaligned,
    BadMagic,
    UnsupportedVersion,
    BadHeader,
    OutOfBounds,
    BadReading,
    NotSorted,
    BadEntry,
};

// Read-only view over a binary system dictionary (written by DictionaryBuilder).
// Every offset and index is validated in Open, so a corrupt file is rejected instead of read out of bounds.
class SystemDictionary {
public:
    // `bytes` must stay alive and unchanged while the dictionary is used, and be 4-byte aligned.
    static std::optional<SystemDictionary> Open(std::span<const std::byte> bytes, DictionaryError* error = nullptr);

    std::size_t reading_count() const { return reading_count_; }
    std::size_t entry_count() const { return entry_count_; }
    std::uint16_t id_count() const { return id_count_; }
    std::uint16_t bos_id() const { return bos_id_; }
    std::uint16_t eos_id() const { return eos_id_; }

    using Visitor = std::function<void(std::size_t length, const DictionaryEntry& entry)>;
    // Visits every entry whose reading is a prefix of `text`, shortest reading first, cheapest entry first.
    void CommonPrefixSearch(std::u16string_view text, const Visitor& visit) const;
    std::vector<DictionaryEntry> Lookup(std::u16string_view reading) const;

    std::int16_t ConnectionCost(std::uint16_t previous_right_id, std::uint16_t next_left_id) const;
    WordType word_type(std::uint16_t id) const;
    std::uint16_t unknown_id() const { return unknown_id_; }
    std::int16_t unknown_cost() const { return unknown_cost_; }

private:
    SystemDictionary() = default;

    std::u16string_view Reading(std::size_t index) const;
    void VisitEntries(std::size_t reading_index, std::size_t length, const Visitor& visit) const;
    std::u16string_view PoolText(std::uint32_t offset, std::uint16_t length) const;

    const std::byte* base_ = nullptr;
    std::size_t reading_count_ = 0;
    std::size_t entry_count_ = 0;
    std::uint16_t id_count_ = 0;
    std::uint16_t bos_id_ = 0;
    std::uint16_t eos_id_ = 0;
    std::uint32_t readings_offset_ = 0;
    std::uint32_t entries_offset_ = 0;
    const char16_t* pool_ = nullptr;
    std::uint32_t matrix_offset_ = 0;
    std::uint32_t word_types_offset_ = 0;
    std::uint16_t unknown_id_ = 0;
    std::int16_t unknown_cost_ = 0;
};

} // namespace astelio
