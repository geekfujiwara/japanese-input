// Conversion accuracy on the evaluation corpus (test plan 10): prints the scores, appends a Markdown
// report, writes the misses, and fails when the sentence accuracy drops below the baseline.
#include "evaluation.h"

#include "astelio/utf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::optional<std::string> ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// "sentence_accuracy<TAB>62.5"; lines starting with '#' are comments.
std::optional<double> ReadBaseline(const fs::path& path)
{
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const std::string key = "sentence_accuracy\t";
        if (line.rfind(key, 0) == 0) {
            char* end = nullptr;
            const double value = std::strtod(line.c_str() + key.size(), &end);
            if (end != line.c_str() + key.size()) {
                return value;
            }
        }
    }
    return std::nullopt;
}

int Usage()
{
    std::cerr << "usage: astelio_eval <system.dic> [--baseline file] [--report file] [--failures file] <corpus.tsv>...\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        return Usage();
    }
    const fs::path dictionary_path = argv[1];
    std::optional<fs::path> baseline_path;
    std::optional<fs::path> report_path;
    std::optional<fs::path> failures_path;
    std::vector<fs::path> corpus_paths;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--baseline" || argument == "--report" || argument == "--failures") && i + 1 < argc) {
            const fs::path value = argv[++i];
            if (argument == "--baseline") {
                baseline_path = value;
            } else if (argument == "--report") {
                report_path = value;
            } else {
                failures_path = value;
            }
        } else if (argument.rfind("--", 0) == 0) {
            return Usage();
        } else {
            corpus_paths.push_back(argument);
        }
    }
    if (corpus_paths.empty()) {
        return Usage();
    }

    const std::optional<std::string> dictionary_text = ReadText(dictionary_path);
    std::vector<std::byte> bytes;
    if (dictionary_text) {
        bytes.resize(dictionary_text->size());
        std::memcpy(bytes.data(), dictionary_text->data(), bytes.size());
    }
    const std::optional<astelio::SystemDictionary> dictionary = astelio::SystemDictionary::Open(bytes);
    if (!dictionary) {
        std::cerr << "cannot open the dictionary " << dictionary_path.string() << '\n';
        return 1;
    }
    astelio::Converter converter(*dictionary);
    // Date and time candidates depend on the clock; a fixed one keeps the scores comparable.
    converter.SetClock([] { return astelio::LocalTime{2026, 1, 15, 10, 0}; });

    astelio::eval::Summary summary;
    std::string failures = "# category\treading\texpected\tbest\n";
    for (const fs::path& path : corpus_paths) {
        const std::optional<std::string> text = ReadText(path);
        if (!text) {
            std::cerr << "cannot read " << path.string() << '\n';
            return 1;
        }
        std::vector<astelio::eval::CorpusError> errors;
        const std::vector<astelio::eval::CorpusEntry> entries = astelio::eval::ParseCorpus(*text, errors);
        for (const astelio::eval::CorpusError& error : errors) {
            std::cerr << path.filename().string() << ':' << error.line << ": " << error.message << '\n';
        }
        if (!errors.empty()) {
            return 1;
        }
        for (const astelio::eval::CorpusEntry& entry : entries) {
            const astelio::eval::EntryResult result = astelio::eval::Evaluate(converter, entry);
            summary.Add(entry, result);
            if (!result.correct) {
                failures += entry.category + '\t' + astelio::Utf16ToUtf8(entry.reading) + '\t' +
                            astelio::Utf16ToUtf8(entry.expected) + '\t' + astelio::Utf16ToUtf8(result.best) + '\n';
            }
        }
    }

    const double accuracy = summary.overall.Accuracy();
    std::string report = "### 変換精度（評価コーパス）\n\n" + astelio::eval::FormatReport(summary);
    int status = 0;
    if (baseline_path) {
        const std::optional<double> baseline = ReadBaseline(*baseline_path);
        if (!baseline) {
            std::cerr << "no sentence_accuracy in " << baseline_path->string() << '\n';
            return 1;
        }
        char text[64];
        std::snprintf(text, sizeof(text), "%.1f", *baseline);
        report += std::string("\n基準値: ") + text + "%（0.5ポイント以上下がると失敗）\n";
        if (astelio::eval::Regressed(accuracy, *baseline)) {
            std::cerr << "sentence accuracy regressed: " << accuracy << "% < baseline " << *baseline << "%\n";
            status = 1;
        }
    }
    std::cout << report << '\n';
    std::cout << "sentence_accuracy=" << accuracy << " top5_accuracy=" << summary.overall.Top5Accuracy()
              << " entries=" << summary.overall.total << '\n';
    if (report_path) {
        std::ofstream(*report_path, std::ios::binary | std::ios::app) << report << '\n';
    }
    if (failures_path) {
        std::ofstream(*failures_path, std::ios::binary) << failures;
    }
    return status;
}
