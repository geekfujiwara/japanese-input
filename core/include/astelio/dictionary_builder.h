#pragma once

#include "astelio/dictionary.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// Part-of-speech connection costs: costs[previous_right_id * size + next_left_id].
struct ConnectionMatrix {
    std::uint16_t size = 0;
    std::uint16_t bos_id = 0;
    std::uint16_t eos_id = 0;
    std::vector<std::int16_t> costs;
    std::vector<WordType> word_types; // per id; missing ids are Content
    std::uint16_t unknown_id = 0;
    std::int16_t unknown_cost = 3000;
    // Meaning model: meaning_costs[earlier * meaning_count + later] between neighbouring segments.
    std::uint16_t meaning_count = 0;
    std::uint16_t neutral_meaning = 0;
    std::vector<std::int16_t> meaning_costs;
    std::vector<bool> gives_meaning; // per id, besides content words (which always do)
};

struct DictionarySourceEntry {
    std::u16string reading;
    std::u16string surface;
    std::uint16_t left_id = 0;
    std::uint16_t right_id = 0;
    std::uint16_t meaning_id = 0;
    std::int16_t cost = 0;
};

struct SourceError {
    std::size_t line = 0; // 1-based
    std::string message;
};

// Connection source (UTF-8 TSV, '#' comments):
//   size<TAB>id_count<TAB>bos_id<TAB>eos_id     first data line
//   right_id<TAB>*<TAB>cost                      default for the row
//   right_id<TAB>left_id<TAB>cost                one cell (overrides the row default)
//   type<TAB>id<TAB>prefix|content|suffix|edge   segment role of a part-of-speech id (default content)
//   unknown<TAB>id<TAB>cost                      part-of-speech id and cost for text not in the dictionary
//   meaning<TAB>count<TAB>neutral_id             meaning model size and the meaning that connects at cost 0
//   mm<TAB>earlier<TAB>later<TAB>cost            cost between the meanings of neighbouring segments (default 0)
//   meaningful<TAB>id                            a non-content part-of-speech id whose words set the segment's meaning
std::optional<ConnectionMatrix> ParseConnectionSource(std::string_view utf8, SourceError* error = nullptr);

// Word source (UTF-8 TSV, '#' comments): reading<TAB>surface<TAB>left_id<TAB>right_id<TAB>meaning_id<TAB>cost.
// Readings are hiragana (as the composer produces them). Bad lines are reported and skipped.
std::vector<DictionarySourceEntry> ParseWordSource(std::string_view utf8, std::uint16_t id_count,
                                                   std::vector<SourceError>& errors);

class DictionaryBuilder {
public:
    explicit DictionaryBuilder(ConnectionMatrix matrix);

    // Returns false (and ignores the entry) when it does not fit the format or the matrix.
    bool Add(DictionarySourceEntry entry);
    std::size_t size() const { return entries_.size(); }

    // Duplicates (same reading, surface and ids) keep the cheapest cost.
    std::vector<std::byte> Build() const;

private:
    ConnectionMatrix matrix_;
    std::vector<DictionarySourceEntry> entries_;
};

} // namespace astelio
