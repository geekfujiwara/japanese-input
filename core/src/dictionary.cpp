#include "astelio/dictionary.h"

#include "dictionary_format.h"

#include <algorithm>
#include <cstring>

namespace astelio {
namespace {

namespace fmt = dictionary_format;

template <typename T>
T Read(const std::byte* base, std::size_t offset)
{
    T value;
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

struct ReadingRecord {
    std::uint32_t text = 0;
    std::uint32_t first_entry = 0;
    std::uint16_t length = 0;
    std::uint16_t entry_count = 0;
};

struct EntryRecord {
    std::uint32_t text = 0;
    std::uint16_t length = 0;
    std::uint16_t left = 0;
    std::uint16_t right = 0;
    std::int16_t cost = 0;
    std::uint16_t meaning = 0;
};

ReadingRecord ReadReading(const std::byte* base, std::uint32_t section, std::size_t index)
{
    const std::size_t offset = section + index * fmt::kReadingRecordSize;
    ReadingRecord record;
    record.text = Read<std::uint32_t>(base, offset);
    record.first_entry = Read<std::uint32_t>(base, offset + 4);
    record.length = Read<std::uint16_t>(base, offset + 8);
    record.entry_count = Read<std::uint16_t>(base, offset + 10);
    return record;
}

EntryRecord ReadEntry(const std::byte* base, std::uint32_t section, std::size_t index)
{
    const std::size_t offset = section + index * fmt::kEntryRecordSize;
    EntryRecord record;
    record.text = Read<std::uint32_t>(base, offset);
    record.length = Read<std::uint16_t>(base, offset + 4);
    record.left = Read<std::uint16_t>(base, offset + 6);
    record.right = Read<std::uint16_t>(base, offset + 8);
    record.cost = Read<std::int16_t>(base, offset + 10);
    record.meaning = Read<std::uint16_t>(base, offset + 12);
    return record;
}

DictionaryEntry ToEntry(const EntryRecord& record, std::u16string_view surface)
{
    DictionaryEntry entry;
    entry.surface = surface;
    entry.left_id = record.left;
    entry.right_id = record.right;
    entry.cost = record.cost;
    entry.meaning_id = record.meaning;
    return entry;
}

bool SectionFits(std::uint64_t offset, std::uint64_t size, std::uint64_t file_size, std::uint64_t alignment)
{
    return offset >= fmt::kHeaderSize && offset % alignment == 0 && offset <= file_size && size <= file_size - offset;
}

bool TextFits(std::uint32_t offset, std::uint16_t length, std::uint32_t pool_units)
{
    return length >= 1 && length <= fmt::kMaxTextLength && static_cast<std::uint64_t>(offset) + length <= pool_units;
}

std::optional<SystemDictionary> Fail(DictionaryError* error, DictionaryError reason)
{
    if (error != nullptr) {
        *error = reason;
    }
    return std::nullopt;
}

} // namespace

std::optional<SystemDictionary> SystemDictionary::Open(std::span<const std::byte> bytes, DictionaryError* error)
{
    if (bytes.size() < fmt::kHeaderSize || bytes.size() > UINT32_MAX) {
        return Fail(error, DictionaryError::TooSmall);
    }
    const std::byte* base = bytes.data();
    if (reinterpret_cast<std::uintptr_t>(base) % 4 != 0) {
        return Fail(error, DictionaryError::Misaligned);
    }
    if (std::memcmp(base, fmt::kMagic, sizeof(fmt::kMagic)) != 0) {
        return Fail(error, DictionaryError::BadMagic);
    }
    if (Read<std::uint32_t>(base, fmt::header::kVersion) != fmt::kVersion) {
        return Fail(error, DictionaryError::UnsupportedVersion);
    }

    const std::uint64_t file_size = bytes.size();
    const std::uint32_t id_count = Read<std::uint32_t>(base, fmt::header::kIdCount);
    const std::uint32_t bos_id = Read<std::uint32_t>(base, fmt::header::kBosId);
    const std::uint32_t eos_id = Read<std::uint32_t>(base, fmt::header::kEosId);
    const std::uint32_t reading_count = Read<std::uint32_t>(base, fmt::header::kReadingCount);
    const std::uint32_t readings_offset = Read<std::uint32_t>(base, fmt::header::kReadingsOffset);
    const std::uint32_t entry_count = Read<std::uint32_t>(base, fmt::header::kEntryCount);
    const std::uint32_t entries_offset = Read<std::uint32_t>(base, fmt::header::kEntriesOffset);
    const std::uint32_t pool_offset = Read<std::uint32_t>(base, fmt::header::kPoolOffset);
    const std::uint32_t pool_units = Read<std::uint32_t>(base, fmt::header::kPoolUnits);
    const std::uint32_t matrix_offset = Read<std::uint32_t>(base, fmt::header::kMatrixOffset);
    const std::uint32_t word_types_offset = Read<std::uint32_t>(base, fmt::header::kWordTypesOffset);
    const std::uint16_t unknown_id = Read<std::uint16_t>(base, fmt::header::kUnknownId);
    const std::uint32_t meaning_count = Read<std::uint32_t>(base, fmt::header::kMeaningCount);
    const std::uint32_t meaning_matrix_offset = Read<std::uint32_t>(base, fmt::header::kMeaningMatrixOffset);
    const std::uint32_t meaning_flags_offset = Read<std::uint32_t>(base, fmt::header::kMeaningFlagsOffset);
    const std::uint16_t neutral_meaning = Read<std::uint16_t>(base, fmt::header::kNeutralMeaning);
    if (Read<std::uint32_t>(base, fmt::header::kFileSize) != file_size || id_count == 0 || id_count > UINT16_MAX ||
        bos_id >= id_count || eos_id >= id_count || unknown_id >= id_count || meaning_count > fmt::kMaxMeanings ||
        (meaning_count > 0 && neutral_meaning >= meaning_count)) {
        return Fail(error, DictionaryError::BadHeader);
    }
    if (!SectionFits(readings_offset, std::uint64_t{reading_count} * fmt::kReadingRecordSize, file_size, 4) ||
        !SectionFits(entries_offset, std::uint64_t{entry_count} * fmt::kEntryRecordSize, file_size, 4) ||
        !SectionFits(pool_offset, std::uint64_t{pool_units} * 2, file_size, 2) ||
        !SectionFits(matrix_offset, std::uint64_t{id_count} * id_count * 2, file_size, 2) ||
        !SectionFits(word_types_offset, id_count, file_size, 1) ||
        !SectionFits(meaning_matrix_offset, std::uint64_t{meaning_count} * meaning_count * 2, file_size, 2) ||
        !SectionFits(meaning_flags_offset, id_count, file_size, 1)) {
        return Fail(error, DictionaryError::OutOfBounds);
    }
    for (std::uint32_t id = 0; id < id_count; ++id) {
        if (std::to_integer<std::uint8_t>(base[word_types_offset + id]) > static_cast<std::uint8_t>(WordType::Edge)) {
            return Fail(error, DictionaryError::BadHeader);
        }
    }

    SystemDictionary dictionary;
    dictionary.base_ = base;
    dictionary.reading_count_ = reading_count;
    dictionary.entry_count_ = entry_count;
    dictionary.id_count_ = static_cast<std::uint16_t>(id_count);
    dictionary.bos_id_ = static_cast<std::uint16_t>(bos_id);
    dictionary.eos_id_ = static_cast<std::uint16_t>(eos_id);
    dictionary.readings_offset_ = readings_offset;
    dictionary.entries_offset_ = entries_offset;
    dictionary.pool_ = reinterpret_cast<const char16_t*>(base + pool_offset);
    dictionary.matrix_offset_ = matrix_offset;
    dictionary.word_types_offset_ = word_types_offset;
    dictionary.unknown_id_ = unknown_id;
    dictionary.unknown_cost_ = Read<std::int16_t>(base, fmt::header::kUnknownCost);
    dictionary.meaning_count_ = static_cast<std::uint16_t>(meaning_count);
    dictionary.neutral_meaning_ = neutral_meaning;
    dictionary.meaning_matrix_offset_ = meaning_matrix_offset;
    dictionary.meaning_flags_offset_ = meaning_flags_offset;

    std::u16string_view previous;
    for (std::size_t i = 0; i < reading_count; ++i) {
        const ReadingRecord record = ReadReading(base, readings_offset, i);
        if (!TextFits(record.text, record.length, pool_units) || record.entry_count == 0 ||
            std::uint64_t{record.first_entry} + record.entry_count > entry_count) {
            return Fail(error, DictionaryError::BadReading);
        }
        const std::u16string_view reading = dictionary.PoolText(record.text, record.length);
        if (i > 0 && !(previous < reading)) {
            return Fail(error, DictionaryError::NotSorted);
        }
        previous = reading;
    }
    for (std::size_t i = 0; i < entry_count; ++i) {
        const EntryRecord record = ReadEntry(base, entries_offset, i);
        if (!TextFits(record.text, record.length, pool_units) || record.left >= id_count || record.right >= id_count ||
            (meaning_count > 0 && record.meaning >= meaning_count)) {
            return Fail(error, DictionaryError::BadEntry);
        }
    }
    return dictionary;
}

std::u16string_view SystemDictionary::PoolText(std::uint32_t offset, std::uint16_t length) const
{
    return {pool_ + offset, length};
}

std::u16string_view SystemDictionary::Reading(std::size_t index) const
{
    const ReadingRecord record = ReadReading(base_, readings_offset_, index);
    return PoolText(record.text, record.length);
}

void SystemDictionary::VisitEntries(std::size_t reading_index, std::size_t length, const Visitor& visit) const
{
    const ReadingRecord reading = ReadReading(base_, readings_offset_, reading_index);
    for (std::size_t i = 0; i < reading.entry_count; ++i) {
        const EntryRecord record = ReadEntry(base_, entries_offset_, reading.first_entry + i);
        visit(length, ToEntry(record, PoolText(record.text, record.length)));
    }
}

void SystemDictionary::Narrow(std::size_t length, char16_t unit, std::size_t& low, std::size_t& high) const
{
    const auto at = [this, length](std::size_t index) -> int {
        const std::u16string_view reading = Reading(index);
        return reading.size() < length ? -1 : static_cast<int>(reading[length - 1]);
    };
    std::size_t first = low;
    std::size_t count = high - low;
    while (count > 0) {
        const std::size_t step = count / 2;
        if (at(first + step) < static_cast<int>(unit)) {
            first += step + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    low = first;
    count = high - low;
    while (count > 0) {
        const std::size_t step = count / 2;
        if (at(first + step) == static_cast<int>(unit)) {
            first += step + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    high = first;
}

void SystemDictionary::CommonPrefixSearch(std::u16string_view text, const Visitor& visit) const
{
    // Invariant: readings in [low, high) all start with text[0, length - 1).
    std::size_t low = 0;
    std::size_t high = reading_count_;
    for (std::size_t length = 1; length <= text.size() && length <= fmt::kMaxTextLength; ++length) {
        Narrow(length, text[length - 1], low, high);
        if (low == high) {
            return;
        }
        if (Reading(low).size() == length) {
            VisitEntries(low, length, visit);
        }
    }
}

void SystemDictionary::ForEachEntry(
    const std::function<void(std::u16string_view reading, const DictionaryEntry& entry)>& visit) const
{
    for (std::size_t index = 0; index < reading_count_; ++index) {
        const ReadingRecord record = ReadReading(base_, readings_offset_, index);
        const std::u16string_view reading = PoolText(record.text, record.length);
        for (std::size_t i = 0; i < record.entry_count; ++i) {
            const EntryRecord entry = ReadEntry(base_, entries_offset_, record.first_entry + i);
            visit(reading, ToEntry(entry, PoolText(entry.text, entry.length)));
        }
    }
}

std::vector<SystemDictionary::Prediction> SystemDictionary::PredictiveSearch(std::u16string_view prefix,
                                                                             std::size_t limit) const
{
    std::vector<Prediction> result;
    if (prefix.empty() || prefix.size() > fmt::kMaxTextLength || limit == 0) {
        return result;
    }
    std::size_t low = 0;
    std::size_t high = reading_count_;
    for (std::size_t length = 1; length <= prefix.size() && low < high; ++length) {
        Narrow(length, prefix[length - 1], low, high);
    }
    // Bounded work per key: look at the cheapest few entries of at most this many readings.
    constexpr std::size_t kMaxReadings = 20000;
    constexpr std::size_t kEntriesPerReading = 3;
    const auto costlier = [](const Prediction& a, const Prediction& b) { return a.entry.cost < b.entry.cost; };
    for (std::size_t index = low; index < high && index - low < kMaxReadings; ++index) {
        const ReadingRecord record = ReadReading(base_, readings_offset_, index);
        const std::u16string_view reading = PoolText(record.text, record.length);
        for (std::size_t i = 0; i < record.entry_count && i < kEntriesPerReading; ++i) {
            const EntryRecord entry = ReadEntry(base_, entries_offset_, record.first_entry + i);
            if (result.size() == limit && entry.cost >= result.front().entry.cost) {
                break; // entries are cheapest first, so the rest of this reading cannot enter either
            }
            Prediction prediction;
            prediction.reading = reading;
            prediction.entry = ToEntry(entry, PoolText(entry.text, entry.length));
            if (result.size() == limit) {
                std::pop_heap(result.begin(), result.end(), costlier);
                result.back() = prediction;
            } else {
                result.push_back(prediction);
            }
            std::push_heap(result.begin(), result.end(), costlier);
        }
    }
    std::sort_heap(result.begin(), result.end(), costlier);
    return result;
}

std::vector<DictionaryEntry> SystemDictionary::Lookup(std::u16string_view reading) const
{
    std::vector<DictionaryEntry> result;
    CommonPrefixSearch(reading, [&result, &reading](std::size_t length, const DictionaryEntry& entry) {
        if (length == reading.size()) {
            result.push_back(entry);
        }
    });
    return result;
}

std::int16_t SystemDictionary::ConnectionCost(std::uint16_t previous_right_id, std::uint16_t next_left_id) const
{
    if (previous_right_id >= id_count_ || next_left_id >= id_count_) {
        return INT16_MAX;
    }
    const std::size_t index = std::size_t{previous_right_id} * id_count_ + next_left_id;
    return Read<std::int16_t>(base_, matrix_offset_ + index * 2);
}

WordType SystemDictionary::word_type(std::uint16_t id) const
{
    if (id >= id_count_) {
        return WordType::Content;
    }
    return static_cast<WordType>(std::to_integer<std::uint8_t>(base_[word_types_offset_ + id]));
}

std::int16_t SystemDictionary::MeaningCost(std::uint16_t earlier, std::uint16_t later) const
{
    if (earlier >= meaning_count_ || later >= meaning_count_ || earlier == neutral_meaning_ ||
        later == neutral_meaning_) {
        return 0;
    }
    const std::size_t index = std::size_t{earlier} * meaning_count_ + later;
    return Read<std::int16_t>(base_, meaning_matrix_offset_ + index * 2);
}

bool SystemDictionary::gives_meaning(std::uint16_t id) const
{
    return id < id_count_ && std::to_integer<std::uint8_t>(base_[meaning_flags_offset_ + id]) != 0;
}

} // namespace astelio
