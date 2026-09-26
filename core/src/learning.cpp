#include "astelio/learning.h"

#include "astelio/utf.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace astelio {
namespace {

constexpr std::string_view kHeader = "# astelio learning 1";

bool IsStorable(std::u16string_view text)
{
    return !text.empty() && text.size() <= LearningHistory::kMaxTextLength &&
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

} // namespace

std::vector<LearningHistory::Entry>::iterator LearningHistory::Find(Kind kind, std::u16string_view reading,
                                                                   std::u16string_view surface)
{
    return std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.kind == kind && entry.reading == reading && entry.surface == surface;
    });
}

bool LearningHistory::Record(Kind kind, std::u16string_view reading, std::u16string_view surface)
{
    if (!IsStorable(reading) || !IsStorable(surface)) {
        return false;
    }
    const auto found = Find(kind, reading, surface);
    if (found != entries_.end()) {
        found->count = found->count == std::numeric_limits<std::uint32_t>::max() ? found->count : found->count + 1;
        found->last_used = ++clock_;
        std::rotate(entries_.begin(), found, found + 1);
        return true;
    }
    entries_.insert(entries_.begin(), Entry{kind, std::u16string(reading), std::u16string(surface), 1, ++clock_});
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
    return true;
}

bool LearningHistory::Remove(Kind kind, std::u16string_view reading, std::u16string_view surface)
{
    const auto found = Find(kind, reading, surface);
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

void LearningHistory::Clear()
{
    entries_.clear();
    entries_.shrink_to_fit();
}

bool LearningHistory::Contains(Kind kind, std::u16string_view reading, std::u16string_view surface) const
{
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.kind == kind && entry.reading == reading && entry.surface == surface;
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
        const bool matches = entry.reading.size() >= prefix.size() &&
                             std::u16string_view(entry.reading).substr(0, prefix.size()) == prefix &&
                             (entry.kind == Kind::Prediction || entry.reading.size() > prefix.size());
        if (matches && std::find(surfaces.begin(), surfaces.end(), entry.surface) == surfaces.end()) {
            surfaces.push_back(entry.surface);
        }
    }
    return surfaces;
}

std::string LearningHistory::Serialize() const
{
    std::string text(kHeader);
    text += '\n';
    for (const Entry& entry : entries_) {
        text += entry.kind == Kind::Prediction ? 'p' : 'c';
        text += '\t';
        text += Utf16ToUtf8(entry.reading);
        text += '\t';
        text += Utf16ToUtf8(entry.surface);
        text += '\t';
        text += std::to_string(entry.count);
        text += '\t';
        text += std::to_string(entry.last_used);
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
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 5 || (fields[0] != "c" && fields[0] != "p")) {
            continue;
        }
        const std::optional<std::u16string> reading = Utf8ToUtf16(fields[1]);
        const std::optional<std::u16string> surface = Utf8ToUtf16(fields[2]);
        const std::optional<std::uint32_t> count = ParseNumber<std::uint32_t>(fields[3]);
        const std::optional<std::uint64_t> last_used = ParseNumber<std::uint64_t>(fields[4]);
        if (!reading || !surface || !count || !last_used || !IsStorable(*reading) || !IsStorable(*surface) ||
            *last_used == std::numeric_limits<std::uint64_t>::max()) {
            continue;
        }
        history.entries_.push_back(
            Entry{fields[0] == "p" ? Kind::Prediction : Kind::Conversion, *reading, *surface, *count, *last_used});
    }
    std::stable_sort(history.entries_.begin(), history.entries_.end(),
                     [](const Entry& a, const Entry& b) { return a.last_used > b.last_used; });
    // Keep the most recent of duplicated lines.
    std::vector<Entry> unique;
    std::unordered_set<std::u16string> seen;
    for (Entry& entry : history.entries_) {
        if (unique.size() >= kMaxEntries) {
            break;
        }
        std::u16string key = entry.reading + u'\t' + entry.surface + (entry.kind == Kind::Prediction ? u'p' : u'c');
        if (seen.insert(std::move(key)).second) {
            unique.push_back(std::move(entry));
        }
    }
    history.entries_ = std::move(unique);
    history.clock_ = history.entries_.empty() ? 0 : history.entries_.front().last_used;
    return history;
}

} // namespace astelio
