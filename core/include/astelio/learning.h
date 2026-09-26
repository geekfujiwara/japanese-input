#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// D-04 / D-05: the words the user chose, so they come first the next time. Kept in memory, most recent first, and
// saved as text by the platform layer (Serialize / Parse). Nothing typed in a password field is recorded.
class LearningHistory {
public:
    enum class Kind : std::uint8_t {
        Conversion = 0, // `surface` was chosen for the segment `reading`
        Prediction = 1, // `surface` was chosen from the predictions while `reading` was typed
    };
    struct Entry {
        Kind kind = Kind::Conversion;
        std::u16string reading;
        std::u16string surface;
        std::uint32_t count = 0;
        std::uint64_t last_used = 0; // a counter, larger is more recent
    };

    static constexpr std::size_t kMaxEntries = 5000;
    static constexpr std::size_t kMaxTextLength = 64;

    // Adds the choice or makes it the most recent. Returns false when it is not recorded (empty or too long).
    bool Record(Kind kind, std::u16string_view reading, std::u16string_view surface);
    // Removes one choice; returns whether it was there.
    bool Remove(Kind kind, std::u16string_view reading, std::u16string_view surface);
    // Removes every choice of `surface` (for deleting a prediction, whose reading is not shown).
    bool RemoveSurface(Kind kind, std::u16string_view surface);
    void Clear();

    bool Contains(Kind kind, std::u16string_view reading, std::u16string_view surface) const;
    // Surfaces chosen for the segment `reading`, most recent first.
    std::vector<std::u16string> Conversions(std::u16string_view reading) const;
    // Surfaces chosen while typing a reading that starts with `prefix` (predictions), or converted from a longer
    // reading that starts with it, most recent first.
    std::vector<std::u16string> Predictions(std::u16string_view prefix, std::size_t limit) const;

    // Most recent first.
    const std::vector<Entry>& Entries() const { return entries_; }
    bool Empty() const { return entries_.empty(); }

    // UTF-8 text: a header line, then kind, reading, surface, count and last use separated by tabs.
    std::string Serialize() const;
    // Lines that are not well formed are skipped; at most kMaxEntries are kept.
    static LearningHistory Parse(std::string_view text);

private:
    std::vector<Entry>::iterator Find(Kind kind, std::u16string_view reading, std::u16string_view surface);

    std::vector<Entry> entries_;
    std::uint64_t clock_ = 0;
};

} // namespace astelio
