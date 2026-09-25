#pragma once

#include "astelio/converter.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace astelio::eval {

// One line of a corpus file: category, reading (hiragana; letters as typed), expected text, other accepted texts.
struct CorpusEntry {
    std::string category;
    std::u16string reading;
    std::u16string expected;
    std::vector<std::u16string> alternatives;
    std::size_t line = 0;
};

struct CorpusError {
    std::size_t line = 0;
    std::string message;
};

// Tab-separated; '#' starts a comment line; alternatives are separated by '|'.
std::vector<CorpusEntry> ParseCorpus(std::string_view text, std::vector<CorpusError>& errors);

struct EntryResult {
    std::u16string best;  // the first candidate of every segment
    bool correct = false; // best is the expected text or an alternative
    bool top5 = false;    // reachable by picking one of the top 5 candidates of each segment
};

inline constexpr std::size_t kTopCandidates = 5;

EntryResult Evaluate(const Converter& converter, const CorpusEntry& entry);

struct Score {
    std::size_t total = 0;
    std::size_t correct = 0;
    std::size_t top5 = 0;

    double Accuracy() const { return total == 0 ? 0.0 : 100.0 * static_cast<double>(correct) / static_cast<double>(total); }
    double Top5Accuracy() const { return total == 0 ? 0.0 : 100.0 * static_cast<double>(top5) / static_cast<double>(total); }
};

struct Summary {
    Score overall;
    std::map<std::string, Score> categories;

    void Add(const CorpusEntry& entry, const EntryResult& result);
};

// Markdown table for the CI job summary.
std::string FormatReport(const Summary& summary);

// CI gate: the sentence accuracy may not drop by `tolerance` points or more below the baseline.
bool Regressed(double accuracy, double baseline, double tolerance = 0.5);

} // namespace astelio::eval
