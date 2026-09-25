#include "evaluation.h"

#include "astelio/utf.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <set>

namespace astelio::eval {
namespace {

std::vector<std::string_view> Split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(separator, begin);
        parts.push_back(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        if (end == std::string_view::npos) {
            return parts;
        }
        begin = end + 1;
    }
}

bool Reachable(const std::vector<ConvertedSegment>& segments, const std::u16string& target)
{
    std::set<std::size_t> positions = {0};
    for (const ConvertedSegment& segment : segments) {
        std::set<std::size_t> next;
        for (const std::size_t position : positions) {
            for (std::size_t i = 0; i < segment.candidates.size() && i < kTopCandidates; ++i) {
                const std::u16string& candidate = segment.candidates[i];
                if (target.compare(position, candidate.size(), candidate) == 0) {
                    next.insert(position + candidate.size());
                }
            }
        }
        positions = std::move(next);
        if (positions.empty()) {
            return false;
        }
    }
    return positions.contains(target.size());
}

std::string Percent(double value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

} // namespace

std::vector<CorpusEntry> ParseCorpus(std::string_view text, std::vector<CorpusError>& errors)
{
    std::vector<CorpusEntry> entries;
    std::size_t number = 0;
    for (std::string_view line : Split(text, '\n')) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = Split(line, '\t');
        if (fields.size() < 3 || fields.size() > 4) {
            errors.push_back({number, "expected 3 or 4 tab-separated fields"});
            continue;
        }
        CorpusEntry entry;
        entry.line = number;
        entry.category = std::string(fields[0]);
        const std::optional<std::u16string> reading = Utf8ToUtf16(fields[1]);
        const std::optional<std::u16string> expected = Utf8ToUtf16(fields[2]);
        if (entry.category.empty() || !reading || reading->empty() || !expected || expected->empty()) {
            errors.push_back({number, "empty or malformed field"});
            continue;
        }
        entry.reading = *reading;
        entry.expected = *expected;
        if (fields.size() == 4 && !fields[3].empty()) {
            for (const std::string_view alternative : Split(fields[3], '|')) {
                const std::optional<std::u16string> text16 = Utf8ToUtf16(alternative);
                if (!text16 || text16->empty()) {
                    errors.push_back({number, "empty or malformed alternative"});
                    continue;
                }
                entry.alternatives.push_back(*text16);
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

EntryResult Evaluate(const Converter& converter, const CorpusEntry& entry)
{
    const std::vector<ConvertedSegment> segments = converter.Convert(entry.reading);
    EntryResult result;
    for (const ConvertedSegment& segment : segments) {
        if (!segment.candidates.empty()) {
            result.best += segment.candidates.front();
        }
    }
    std::vector<const std::u16string*> accepted = {&entry.expected};
    for (const std::u16string& alternative : entry.alternatives) {
        accepted.push_back(&alternative);
    }
    for (const std::u16string* text : accepted) {
        result.correct = result.correct || result.best == *text;
        result.top5 = result.top5 || Reachable(segments, *text);
    }
    return result;
}

void Summary::Add(const CorpusEntry& entry, const EntryResult& result)
{
    for (Score* score : {&overall, &categories[entry.category]}) {
        ++score->total;
        score->correct += result.correct ? 1 : 0;
        score->top5 += result.top5 ? 1 : 0;
    }
}

std::string FormatReport(const Summary& summary)
{
    std::string report = "| 分類 | 件数 | 文正解率 | 上位5件の正解率 |\n| --- | ---: | ---: | ---: |\n";
    const auto row = [&report](const std::string& name, const Score& score) {
        report += "| " + name + " | " + std::to_string(score.total) + " | " + Percent(score.Accuracy()) + "% | " +
                  Percent(score.Top5Accuracy()) + "% |\n";
    };
    for (const auto& [name, score] : summary.categories) {
        row(name, score);
    }
    row("**全体**", summary.overall);
    return report;
}

bool Regressed(double accuracy, double baseline, double tolerance)
{
    return baseline - accuracy >= tolerance - 1e-9;
}

} // namespace astelio::eval
