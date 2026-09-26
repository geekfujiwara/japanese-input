#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace astelio {

// D-04 / D-05: the words the user chose, so they come first the next time. Kept in memory, most recent first, and
// saved as text by the platform layer (Serialize / Parse). Nothing typed in a password field is recorded.
class LearningHistory {
public:
    enum class Kind : std::uint8_t {
        Conversion = 0,   // `surface` was chosen for the segment `reading`
        Prediction = 1,   // `surface` was chosen from the predictions while `reading` was typed
        Pair = 2,         // `surface` was chosen for `reading` right after the word `context`
        Segmentation = 3, // `reading` was split into segments as in `surface` ("わたし|は|にほんご")
    };
    struct Entry {
        Kind kind = Kind::Conversion;
        std::u16string context; // Pair only: the word committed before
        std::u16string reading;
        std::u16string surface;
        std::uint32_t count = 0;
        std::uint64_t last_used = 0; // a counter, larger is more recent
        std::int64_t time = 0;       // seconds since 1970 of the last use (0: unknown, from an old file)
    };

    static constexpr std::size_t kMaxEntries = 5000;
    static constexpr std::size_t kMaxTextLength = 64;
    static constexpr char16_t kSegmentSeparator = u'|';

    LearningHistory();

    // The wall clock for Entry::time (tests fix it).
    void SetClock(std::function<std::int64_t()> clock) { clock_ = std::move(clock); }

    // Adds the choice or makes it the most recent. Returns false when it is not recorded (empty or too long).
    bool Record(Kind kind, std::u16string_view reading, std::u16string_view surface, std::u16string_view context = {});
    // Removes one choice; returns whether it was there.
    bool Remove(Kind kind, std::u16string_view reading, std::u16string_view surface, std::u16string_view context = {});
    // Removes every choice of `surface` (for deleting a prediction, whose reading is not shown).
    bool RemoveSurface(Kind kind, std::u16string_view surface);
    // Removes what was used at `since` or later (seconds since 1970); returns how many.
    std::size_t RemoveSince(std::int64_t since);
    void Clear();

    bool Contains(Kind kind, std::u16string_view reading, std::u16string_view surface,
                  std::u16string_view context = {}) const;
    // Surfaces chosen for the segment `reading`, most recent first.
    std::vector<std::u16string> Conversions(std::u16string_view reading) const;
    // Surfaces chosen for `reading` right after `context`, most recent first.
    std::vector<std::u16string> Pairs(std::u16string_view context, std::u16string_view reading) const;
    // Surfaces chosen while typing a reading that starts with `prefix` (predictions), or converted from a longer
    // reading that starts with it, most recent first.
    std::vector<std::u16string> Predictions(std::u16string_view prefix, std::size_t limit) const;
    // The segment lengths the user chose for `reading`, if any.
    std::optional<std::vector<std::size_t>> Segmentation(std::u16string_view reading) const;
    // The Segmentation surface for segments of these readings.
    static std::u16string JoinSegments(const std::vector<std::u16string>& readings);

    // Most recent first.
    const std::vector<Entry>& Entries() const { return entries_; }
    bool Empty() const { return entries_.empty(); }

    // UTF-8 text: a header line, then kind, context, reading, surface, count, last use and time separated by tabs.
    // Files of the first version (without context and time) are read too.
    std::string Serialize() const;
    // Lines that are not well formed are skipped; at most kMaxEntries are kept.
    static LearningHistory Parse(std::string_view text);

private:
    std::vector<Entry>::iterator Find(Kind kind, std::u16string_view reading, std::u16string_view surface,
                                      std::u16string_view context);

    std::vector<Entry> entries_;
    std::uint64_t counter_ = 0;
    std::function<std::int64_t()> clock_;
};

} // namespace astelio
