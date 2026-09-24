#include "astelio/dictionary_builder.h"

#include "astelio/utf.h"
#include "dictionary_format.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>

namespace astelio {
namespace {

namespace fmt = dictionary_format;

inline constexpr std::size_t kMaxConnectionIds = 4096;

template <typename Callback>
void ForEachLine(std::string_view text, Callback&& callback)
{
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    std::size_t number = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++number;
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        callback(number, line);
    }
}

std::vector<std::string_view> SplitTabs(std::string_view line)
{
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t tab = line.find('\t');
        fields.push_back(line.substr(0, tab));
        if (tab == std::string_view::npos) {
            return fields;
        }
        line.remove_prefix(tab + 1);
    }
}

template <typename T>
bool ParseInteger(std::string_view text, T& value)
{
    const char* end = text.data() + text.size();
    const auto [pointer, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && pointer == end && !text.empty();
}

bool ValidText(const std::u16string& text)
{
    return !text.empty() && text.size() <= fmt::kMaxTextLength &&
           std::none_of(text.begin(), text.end(), [](char16_t c) { return c < 0x20 || c == 0x7F; });
}

template <typename T>
void Write(std::vector<std::byte>& out, std::size_t offset, T value)
{
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

} // namespace

std::optional<ConnectionMatrix> ParseConnectionSource(std::string_view utf8, SourceError* error)
{
    std::optional<ConnectionMatrix> matrix;
    std::vector<std::pair<std::uint16_t, std::int16_t>> row_defaults;
    std::vector<std::tuple<std::uint16_t, std::uint16_t, std::int16_t>> cells;
    SourceError failure;
    bool failed = false;
    const auto fail = [&](std::size_t line, const char* message) {
        if (!failed) {
            failed = true;
            failure = SourceError{line, message};
        }
    };

    ForEachLine(utf8, [&](std::size_t number, std::string_view line) {
        if (failed) {
            return;
        }
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (!matrix) {
            std::uint16_t size = 0;
            std::uint16_t bos = 0;
            std::uint16_t eos = 0;
            if (fields.size() != 4 || fields[0] != "size" || !ParseInteger(fields[1], size) ||
                !ParseInteger(fields[2], bos) || !ParseInteger(fields[3], eos) || size == 0 ||
                size > kMaxConnectionIds || bos >= size || eos >= size) {
                fail(number, "expected size<TAB>id_count<TAB>bos_id<TAB>eos_id");
                return;
            }
            matrix = ConnectionMatrix{size, bos, eos, std::vector<std::int16_t>(std::size_t{size} * size, 0)};
            return;
        }
        std::uint16_t right = 0;
        std::uint16_t left = 0;
        std::int16_t cost = 0;
        if (fields.size() != 3 || !ParseInteger(fields[0], right) || right >= matrix->size ||
            !ParseInteger(fields[2], cost)) {
            fail(number, "expected right_id<TAB>left_id|*<TAB>cost");
            return;
        }
        if (fields[1] == "*") {
            row_defaults.emplace_back(right, cost);
        } else if (ParseInteger(fields[1], left) && left < matrix->size) {
            cells.emplace_back(right, left, cost);
        } else {
            fail(number, "left_id is out of range");
        }
    });

    if (!failed && !matrix) {
        fail(1, "missing size line");
    }
    if (failed) {
        if (error != nullptr) {
            *error = failure;
        }
        return std::nullopt;
    }
    const std::size_t size = matrix->size;
    for (const auto& [right, cost] : row_defaults) {
        std::fill_n(matrix->costs.begin() + static_cast<std::ptrdiff_t>(right * size), size, cost);
    }
    for (const auto& [right, left, cost] : cells) {
        matrix->costs[std::size_t{right} * size + left] = cost;
    }
    return matrix;
}

std::vector<DictionarySourceEntry> ParseWordSource(std::string_view utf8, std::uint16_t id_count,
                                                   std::vector<SourceError>& errors)
{
    std::vector<DictionarySourceEntry> entries;
    ForEachLine(utf8, [&](std::size_t number, std::string_view line) {
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 6) {
            errors.push_back({number, "expected 6 fields"});
            return;
        }
        std::optional<std::u16string> reading = Utf8ToUtf16(fields[0]);
        std::optional<std::u16string> surface = Utf8ToUtf16(fields[1]);
        if (!reading || !surface || !ValidText(*reading) || !ValidText(*surface)) {
            errors.push_back({number, "invalid reading or surface"});
            return;
        }
        DictionarySourceEntry entry;
        if (!ParseInteger(fields[2], entry.left_id) || !ParseInteger(fields[3], entry.right_id) ||
            entry.left_id >= id_count || entry.right_id >= id_count) {
            errors.push_back({number, "invalid part-of-speech id"});
            return;
        }
        if (!ParseInteger(fields[4], entry.meaning_id) || !ParseInteger(fields[5], entry.cost)) {
            errors.push_back({number, "invalid meaning id or cost"});
            return;
        }
        entry.reading = std::move(*reading);
        entry.surface = std::move(*surface);
        entries.push_back(std::move(entry));
    });
    return entries;
}

DictionaryBuilder::DictionaryBuilder(ConnectionMatrix matrix) : matrix_(std::move(matrix)) {}

bool DictionaryBuilder::Add(DictionarySourceEntry entry)
{
    if (!ValidText(entry.reading) || !ValidText(entry.surface) || entry.left_id >= matrix_.size ||
        entry.right_id >= matrix_.size) {
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

std::vector<std::byte> DictionaryBuilder::Build() const
{
    if (matrix_.size == 0 || matrix_.costs.size() != std::size_t{matrix_.size} * matrix_.size) {
        return {};
    }
    std::vector<const DictionarySourceEntry*> sorted;
    sorted.reserve(entries_.size());
    for (const DictionarySourceEntry& entry : entries_) {
        sorted.push_back(&entry);
    }
    std::sort(sorted.begin(), sorted.end(), [](const DictionarySourceEntry* a, const DictionarySourceEntry* b) {
        return std::tie(a->reading, a->surface, a->left_id, a->right_id, a->cost) <
               std::tie(b->reading, b->surface, b->left_id, b->right_id, b->cost);
    });
    sorted.erase(std::unique(sorted.begin(), sorted.end(),
                             [](const DictionarySourceEntry* a, const DictionarySourceEntry* b) {
                                 return a->reading == b->reading && a->surface == b->surface &&
                                        a->left_id == b->left_id && a->right_id == b->right_id;
                             }),
                 sorted.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const DictionarySourceEntry* a, const DictionarySourceEntry* b) {
        return std::tie(a->reading, a->cost) < std::tie(b->reading, b->cost);
    });

    struct Group {
        std::size_t begin;
        std::size_t count;
    };
    std::vector<Group> groups;
    for (std::size_t i = 0; i < sorted.size();) {
        std::size_t j = i;
        while (j < sorted.size() && sorted[j]->reading == sorted[i]->reading) {
            ++j;
        }
        groups.push_back({i, std::min<std::size_t>(j - i, std::numeric_limits<std::uint16_t>::max())});
        i = j;
    }

    std::u16string pool;
    std::vector<std::uint32_t> reading_text(groups.size());
    std::vector<std::uint32_t> surface_text(sorted.size());
    std::size_t kept = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        reading_text[g] = static_cast<std::uint32_t>(pool.size());
        pool += sorted[groups[g].begin]->reading;
        for (std::size_t k = 0; k < groups[g].count; ++k) {
            const DictionarySourceEntry& entry = *sorted[groups[g].begin + k];
            if (entry.surface == entry.reading) {
                surface_text[groups[g].begin + k] = reading_text[g];
            } else {
                surface_text[groups[g].begin + k] = static_cast<std::uint32_t>(pool.size());
                pool += entry.surface;
            }
        }
        kept += groups[g].count;
    }

    const std::size_t readings_offset = fmt::kHeaderSize;
    const std::size_t entries_offset = readings_offset + groups.size() * fmt::kReadingRecordSize;
    const std::size_t pool_offset = entries_offset + kept * fmt::kEntryRecordSize;
    const std::size_t matrix_offset = pool_offset + pool.size() * 2;
    const std::size_t file_size = matrix_offset + matrix_.costs.size() * 2;
    if (file_size > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    std::vector<std::byte> out(file_size, std::byte{0});
    std::memcpy(out.data(), fmt::kMagic, sizeof(fmt::kMagic));
    Write<std::uint32_t>(out, fmt::header::kVersion, fmt::kVersion);
    Write<std::uint32_t>(out, fmt::header::kIdCount, matrix_.size);
    Write<std::uint32_t>(out, fmt::header::kBosId, matrix_.bos_id);
    Write<std::uint32_t>(out, fmt::header::kEosId, matrix_.eos_id);
    Write<std::uint32_t>(out, fmt::header::kReadingCount, static_cast<std::uint32_t>(groups.size()));
    Write<std::uint32_t>(out, fmt::header::kReadingsOffset, static_cast<std::uint32_t>(readings_offset));
    Write<std::uint32_t>(out, fmt::header::kEntryCount, static_cast<std::uint32_t>(kept));
    Write<std::uint32_t>(out, fmt::header::kEntriesOffset, static_cast<std::uint32_t>(entries_offset));
    Write<std::uint32_t>(out, fmt::header::kPoolOffset, static_cast<std::uint32_t>(pool_offset));
    Write<std::uint32_t>(out, fmt::header::kPoolUnits, static_cast<std::uint32_t>(pool.size()));
    Write<std::uint32_t>(out, fmt::header::kMatrixOffset, static_cast<std::uint32_t>(matrix_offset));
    Write<std::uint32_t>(out, fmt::header::kFileSize, static_cast<std::uint32_t>(file_size));

    std::size_t entry_index = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const std::size_t record = readings_offset + g * fmt::kReadingRecordSize;
        Write<std::uint32_t>(out, record, reading_text[g]);
        Write<std::uint32_t>(out, record + 4, static_cast<std::uint32_t>(entry_index));
        Write<std::uint16_t>(out, record + 8, static_cast<std::uint16_t>(sorted[groups[g].begin]->reading.size()));
        Write<std::uint16_t>(out, record + 10, static_cast<std::uint16_t>(groups[g].count));
        for (std::size_t k = 0; k < groups[g].count; ++k, ++entry_index) {
            const DictionarySourceEntry& entry = *sorted[groups[g].begin + k];
            const std::size_t at = entries_offset + entry_index * fmt::kEntryRecordSize;
            Write<std::uint32_t>(out, at, surface_text[groups[g].begin + k]);
            Write<std::uint16_t>(out, at + 4, static_cast<std::uint16_t>(entry.surface.size()));
            Write<std::uint16_t>(out, at + 6, entry.left_id);
            Write<std::uint16_t>(out, at + 8, entry.right_id);
            Write<std::int16_t>(out, at + 10, entry.cost);
        }
    }
    if (!pool.empty()) {
        std::memcpy(out.data() + pool_offset, pool.data(), pool.size() * 2);
    }
    std::memcpy(out.data() + matrix_offset, matrix_.costs.data(), matrix_.costs.size() * 2);
    return out;
}

} // namespace astelio
