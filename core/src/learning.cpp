#include "astelio/learning.h"

#include "astelio/utf.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <unordered_set>
#include <utility>

namespace astelio {
namespace {

constexpr std::string_view kHeader = "# astelio learning 2";
// Segmentation surfaces hold the reading and a separator per segment.
constexpr std::size_t kMaxSurfaceLength = LearningHistory::kMaxTextLength * 2;

bool IsStorable(std::u16string_view text, std::size_t limit)
{
    return !text.empty() && text.size() <= limit &&
           std::none_of(text.begin(), text.end(), [](char16_t c) { return c < 0x20 || c == 0x7F; });
}

template <typename Number>
std::optional<Number> ParseNumber(std::string_view text)
{
    Number value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
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

char KindCode(LearningHistory::Kind kind)
{
    switch (kind) {
    case LearningHistory::Kind::Prediction: return 'p';
    case LearningHistory::Kind::Pair: return 'w';
    case LearningHistory::Kind::Segmentation: return 's';
    case LearningHistory::Kind::Conversion: break;
    }
    return 'c';
}

std::optional<LearningHistory::Kind> KindOf(std::string_view code)
{
    if (code == "c") {
        return LearningHistory::Kind::Conversion;
    }
    if (code == "p") {
        return LearningHistory::Kind::Prediction;
    }
    if (code == "w") {
        return LearningHistory::Kind::Pair;
    }
    if (code == "s") {
        return LearningHistory::Kind::Segmentation;
    }
    return std::nullopt;
}

// A segmentation must spell the reading, with non-empty segments.
bool IsSegmentation(std::u16string_view reading, std::u16string_view surface)
{
    std::u16string joined;
    std::size_t length = 0;
    for (const char16_t c : surface) {
        if (c == LearningHistory::kSegmentSeparator) {
            if (length == 0) {
                return false;
            }
            length = 0;
            continue;
        }
        joined.push_back(c);
        ++length;
    }
    return length > 0 && joined == reading;
}

bool IsValid(LearningHistory::Kind kind, std::u16string_view context, std::u16string_view reading,
             std::u16string_view surface)
{
    if (!IsStorable(reading, LearningHistory::kMaxTextLength) || !IsStorable(surface, kMaxSurfaceLength)) {
        return false;
    }
    if (kind == LearningHistory::Kind::Pair) {
        return IsStorable(context, LearningHistory::kMaxTextLength);
    }
    if (!context.empty()) {
        return false;
    }
    return kind != LearningHistory::Kind::Segmentation || IsSegmentation(reading, surface);
}

} // namespace

LearningHistory::LearningHistory()
    : clock_([] {
          return std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
              .count();
      })
{
}

std::vector<LearningHistory::Entry>::iterator LearningHistory::Find(Kind kind, std::u16string_view reading,
                                                                   std::u16string_view surface,
                                                                   std::u16string_view context)
{
    return std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.kind == kind && entry.reading == reading && entry.surface == surface && entry.context == context;
    });
}

bool LearningHistory::Record(Kind kind, std::u16string_view reading, std::u16string_view surface,
                             std::u16string_view context)
{
    if (!IsValid(kind, context, reading, surface)) {
        return false;
    }
    const std::int64_t now = clock_ ? clock_() : 0;
    if (kind == Kind::Segmentation) {
        // One segmentation per reading: the latest.
        std::erase_if(entries_, [&](const Entry& entry) {
            return entry.kind == Kind::Segmentation && entry.reading == reading && entry.surface != surface;
        });
    }
    const auto found = Find(kind, reading, surface, context);
    if (found != entries_.end()) {
        found->count = found->count == std::numeric_limits<std::uint32_t>::max() ? found->count : found->count + 1;
        found->last_used = ++counter_;
        found->time = now;
        std::rotate(entries_.begin(), found, found + 1);
        return true;
    }
    entries_.insert(entries_.begin(), Entry{kind, std::u16string(context), std::u16string(reading),
                                            std::u16string(surface), 1, ++counter_, now});
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
    return true;
}

bool LearningHistory::Remove(Kind kind, std::u16string_view reading, std::u16string_view surface,
                             std::u16string_view context)
{
    const auto found = Find(kind, reading, surface, context);
    if (found == entries_.end()) {
        return false;
    }
    entries_.erase(found);
    return true;
}

bool LearningHistory::RemoveSurface(Kind kind, std::u16string_view surface)
{
    const std::size_t before = entries_.size();
    std::erase_if(entries_, [&](const Entry& entry) { return entry.kind == kind && entry.surface == surface; });
    return entries_.size() != before;
}

std::size_t LearningHistory::RemoveSince(std::int64_t since)
{
    return static_cast<std::size_t>(
        std::erase_if(entries_, [since](const Entry& entry) { return entry.time >= since; }));
}

void LearningHistory::Clear()
{
    entries_.clear();
    entries_.shrink_to_fit();
}

bool LearningHistory::Contains(Kind kind, std::u16string_view reading, std::u16string_view surface,
                               std::u16string_view context) const
{
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.kind == kind && entry.reading == reading && entry.surface == surface && entry.context == context;
    });
}

std::vector<std::u16string> LearningHistory::Conversions(std::u16string_view reading) const
{
    std::vector<std::u16string> surfaces;
    for (const Entry& entry : entries_) {
        if (entry.kind == Kind::Conversion && entry.reading == reading) {
            surfaces.push_back(entry.surface);
        }
    }
    return surfaces;
}

std::vector<std::u16string> LearningHistory::Pairs(std::u16string_view context, std::u16string_view reading) const
{
    std::vector<std::u16string> surfaces;
    if (context.empty()) {
        return surfaces;
    }
    for (const Entry& entry : entries_) {
        if (entry.kind == Kind::Pair && entry.context == context && entry.reading == reading) {
            surfaces.push_back(entry.surface);
        }
    }
    return surfaces;
}

std::vector<std::u16string> LearningHistory::Predictions(std::u16string_view prefix, std::size_t limit) const
{
    std::vector<std::u16string> surfaces;
    if (prefix.empty()) {
        return surfaces;
    }
    for (const Entry& entry : entries_) {
        if (surfaces.size() >= limit) {
            break;
        }
        if (entry.kind != Kind::Conversion && entry.kind != Kind::Prediction) {
            continue;
        }
        const bool matches = entry.reading.size() >= prefix.size() &&
                             std::u16string_view(entry.reading).substr(0, prefix.size()) == prefix &&
                             (entry.kind == Kind::Prediction || entry.reading.size() > prefix.size());
        if (matches && std::find(surfaces.begin(), surfaces.end(), entry.surface) == surfaces.end()) {
            surfaces.push_back(entry.surface);
        }
    }
    return surfaces;
}

std::optional<std::vector<std::size_t>> LearningHistory::Segmentation(std::u16string_view reading) const
{
    for (const Entry& entry : entries_) {
        if (entry.kind != Kind::Segmentation || entry.reading != reading) {
            continue;
        }
        std::vector<std::size_t> lengths(1, 0);
        for (const char16_t c : entry.surface) {
            if (c == kSegmentSeparator) {
                lengths.push_back(0);
            } else {
                ++lengths.back();
            }
        }
        return lengths;
    }
    return std::nullopt;
}

std::u16string LearningHistory::JoinSegments(const std::vector<std::u16string>& readings)
{
    std::u16string joined;
    for (std::size_t i = 0; i < readings.size(); ++i) {
        if (i > 0) {
            joined.push_back(kSegmentSeparator);
        }
        joined += readings[i];
    }
    return joined;
}

std::string LearningHistory::Serialize() const
{
    std::string text(kHeader);
    text += '\n';
    for (const Entry& entry : entries_) {
        text += KindCode(entry.kind);
        for (const std::u16string* field : {&entry.context, &entry.reading, &entry.surface}) {
            text += '\t';
            text += Utf16ToUtf8(*field);
        }
        text += '\t';
        text += std::to_string(entry.count);
        text += '\t';
        text += std::to_string(entry.last_used);
        text += '\t';
        text += std::to_string(entry.time);
        text += '\n';
    }
    return text;
}

LearningHistory LearningHistory::Parse(std::string_view text)
{
    LearningHistory history;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() == 5) {
            // Version 1: kind, reading, surface, count, last use.
            fields.insert(fields.begin() + 1, std::string_view());
            fields.push_back("0");
        }
        if (fields.size() != 7) {
            continue;
        }
        const std::optional<Kind> kind = KindOf(fields[0]);
        const std::optional<std::u16string> context = Utf8ToUtf16(fields[1]);
        const std::optional<std::u16string> reading = Utf8ToUtf16(fields[2]);
        const std::optional<std::u16string> surface = Utf8ToUtf16(fields[3]);
        const std::optional<std::uint32_t> count = ParseNumber<std::uint32_t>(fields[4]);
        const std::optional<std::uint64_t> last_used = ParseNumber<std::uint64_t>(fields[5]);
        const std::optional<std::int64_t> time = ParseNumber<std::int64_t>(fields[6]);
        if (!kind || !context || !reading || !surface || !count || !last_used || !time ||
            !IsValid(*kind, *context, *reading, *surface) ||
            *last_used == std::numeric_limits<std::uint64_t>::max()) {
            continue;
        }
        history.entries_.push_back(Entry{*kind, *context, *reading, *surface, *count, *last_used, *time});
    }
    std::stable_sort(history.entries_.begin(), history.entries_.end(),
                     [](const Entry& a, const Entry& b) { return a.last_used > b.last_used; });
    // Keep the most recent of duplicated lines, and one segmentation per reading.
    std::vector<Entry> unique;
    std::unordered_set<std::u16string> seen;
    for (Entry& entry : history.entries_) {
        if (unique.size() >= kMaxEntries) {
            break;
        }
        std::u16string key(1, static_cast<char16_t>(u'0' + static_cast<int>(entry.kind)));
        key += entry.context + u'\t' + entry.reading;
        if (entry.kind != Kind::Segmentation) {
            key += u'\t' + entry.surface;
        }
        if (seen.insert(std::move(key)).second) {
            unique.push_back(std::move(entry));
        }
    }
    history.entries_ = std::move(unique);
    history.counter_ = history.entries_.empty() ? 0 : history.entries_.front().last_used;
    return history;
}

} // namespace astelio
