// Performance of the core (test plan 5, P-01 to P-04): key handling, conversion of 10 and 30 characters and
// predictions, timed on the readings of the evaluation corpus, also with a user dictionary of 10,000 words.
// Prints p50 / p95 / p99, appends a Markdown report, and fails when a p95 is over its target.
#include "evaluation.h"

#include "astelio/character_rules.h"
#include "astelio/converter.h"
#include "astelio/input_session.h"
#include "astelio/romaji_table.h"
#include "astelio/user_dictionary.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::optional<std::string> ReadText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct Measure {
    std::string id;
    std::string name;
    double target_ms = 0;
    std::vector<double> samples_ms;

    double Percentile(std::size_t percent) const
    {
        if (samples_ms.empty()) {
            return 0;
        }
        std::vector<double> sorted = samples_ms;
        std::sort(sorted.begin(), sorted.end());
        return sorted[std::min(sorted.size() - 1, sorted.size() * percent / 100)];
    }
    bool Passed() const { return Percentile(95) <= target_ms; }
};

double Time(const std::function<void()>& work)
{
    const Clock::time_point start = Clock::now();
    work();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Windows of `length` characters over the readings put end to end (the conversion does not care where they cut).
std::vector<std::u16string> Windows(const std::vector<std::u16string>& readings, std::size_t length)
{
    std::u16string all;
    for (const std::u16string& reading : readings) {
        all += reading;
    }
    std::vector<std::u16string> windows;
    for (std::size_t at = 0; at + length <= all.size(); at += length / 2) {
        windows.push_back(all.substr(at, length));
    }
    return windows;
}

// The keys that type `reading` with the default table (shortest spelling of the longest kana first); nullopt when
// some character has no spelling (letters and digits in the corpus).
std::optional<std::u16string> Keys(const std::u16string& reading, const std::map<std::u16string, std::u16string>& spelling)
{
    std::u16string keys;
    for (std::size_t at = 0; at < reading.size();) {
        bool found = false;
        for (std::size_t length = std::min<std::size_t>(3, reading.size() - at); length > 0; --length) {
            const auto rule = spelling.find(reading.substr(at, length));
            if (rule != spelling.end()) {
                keys += rule->second;
                at += length;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return keys;
}

std::map<std::u16string, std::u16string> Spellings(const astelio::RomajiTable& table)
{
    std::map<std::u16string, std::u16string> spelling;
    for (const auto& [input, rule] : table.rules()) {
        if (!rule.pending.empty() || rule.output.empty()) {
            continue;
        }
        const auto known = spelling.find(rule.output);
        if (known == spelling.end() || input.size() < known->second.size()) {
            spelling[rule.output] = input;
        }
    }
    // ん before a vowel or y needs "nn"; "nn" is always safe.
    spelling[u"ん"] = u"nn";
    return spelling;
}

// 10,000 words with readings made of common kana, and 100 suppressed words (P-02 with the user dictionary).
astelio::UserDictionary LargeUserDictionary()
{
    static constexpr char16_t kKana[] = u"あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわん";
    constexpr std::size_t kKanaCount = std::size(kKana) - 1;
    std::uint32_t state = 12345;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    astelio::UserDictionary dictionary;
    for (std::size_t i = 0; i < astelio::UserDictionary::kMaxWords; ++i) {
        std::u16string reading;
        for (std::size_t length = 3 + next() % 4; length > 0; --length) {
            reading.push_back(kKana[next() % kKanaCount]);
        }
        const bool suppressed = i % 100 == 0;
        std::u16string surface = u"語" + std::u16string(1, static_cast<char16_t>(u'一' + i % 1000)) +
                                 std::u16string(1, static_cast<char16_t>(u'ぁ' + i % 80));
        dictionary.Add({suppressed ? std::u16string() : reading, std::move(surface),
                        suppressed ? astelio::UserDictionary::PartOfSpeech::Suppressed
                                   : astelio::UserDictionary::PartOfSpeech::Noun,
                        u""});
    }
    return dictionary;
}

std::string Format(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", value);
    return text;
}

int Usage()
{
    std::cerr << "usage: astelio_bench <system.dic> [--report file] [--samples count] <corpus.tsv>...\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        return Usage();
    }
    std::string report_path;
    std::size_t min_samples = 1000;
    std::vector<std::string> corpus_paths;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (argument == "--samples" && i + 1 < argc) {
            min_samples = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (argument.rfind("--", 0) == 0) {
            return Usage();
        } else {
            corpus_paths.push_back(argument);
        }
    }

    const std::optional<std::string> dictionary_text = ReadText(argv[1]);
    std::vector<std::byte> bytes;
    if (dictionary_text) {
        bytes.resize(dictionary_text->size());
        std::memcpy(bytes.data(), dictionary_text->data(), bytes.size());
    }
    const std::optional<astelio::SystemDictionary> dictionary = astelio::SystemDictionary::Open(bytes);
    if (!dictionary) {
        std::cerr << "cannot open the dictionary\n";
        return 1;
    }
    std::vector<std::u16string> readings;
    for (const std::string& path : corpus_paths) {
        const std::optional<std::string> text = ReadText(path);
        std::vector<astelio::eval::CorpusError> errors;
        if (!text) {
            std::cerr << "cannot read " << path << '\n';
            return 1;
        }
        for (const astelio::eval::CorpusEntry& entry : astelio::eval::ParseCorpus(*text, errors)) {
            readings.push_back(entry.reading);
        }
    }
    if (readings.empty()) {
        std::cerr << "no readings\n";
        return 1;
    }

    astelio::Converter converter(*dictionary);
    converter.SetClock([] { return astelio::LocalTime{2026, 1, 15, 10, 0}; });
    const astelio::UserDictionary user_words = LargeUserDictionary();
    astelio::Converter converter_with_user(*dictionary);
    converter_with_user.SetClock([] { return astelio::LocalTime{2026, 1, 15, 10, 0}; });
    converter_with_user.SetUserDictionary(&user_words);

    const std::vector<std::u16string> tens = Windows(readings, 10);
    const std::vector<std::u16string> thirties = Windows(readings, 30);
    std::vector<Measure> measures = {
        {"P-01", "key handling in the core (a letter typed, with predictions and もしかして)", 16, {}},
        {"P-02", "conversion of 10 characters", 30, {}},
        {"P-03", "conversion of 30 characters", 60, {}},
        {"P-04", "predictions", 20, {}},
        {"P-02", "conversion of 10 characters with 10,000 user words", 30, {}},
    };
    const auto repeat = [min_samples](const auto& items, const auto& work, Measure& measure) {
        for (const auto& item : items) {
            work(item); // warm-up: the dictionary pages are loaded once
        }
        while (!items.empty() && measure.samples_ms.size() < min_samples) {
            for (const auto& item : items) {
                measure.samples_ms.push_back(Time([&] { work(item); }));
            }
        }
    };

    // P-01: every letter of the corpus sentences typed into a session, Space and Escape between sentences.
    const std::map<std::u16string, std::u16string> spelling = Spellings(astelio::RomajiTable::Default());
    std::vector<std::u16string> typed;
    for (const std::u16string& reading : readings) {
        if (std::optional<std::u16string> keys = Keys(reading, spelling)) {
            typed.push_back(std::move(*keys));
        }
    }
    astelio::InputSession session(astelio::RomajiTable::Default(), astelio::CharacterSettings{});
    session.SetConverter(&converter);
    const auto type_sentence = [&session, &measures](const std::u16string& keys, bool record) {
        for (const char16_t key : keys) {
            const double ms = Time([&] { session.Handle(astelio::KeyEvent{astelio::KeyKind::Character, key}); });
            if (record) {
                measures[0].samples_ms.push_back(ms);
            }
        }
        session.Handle(astelio::KeyEvent{astelio::KeyKind::Space, 0});
        session.Handle(astelio::KeyEvent{astelio::KeyKind::Escape, 0});
        session.Handle(astelio::KeyEvent{astelio::KeyKind::Escape, 0});
    };
    for (const std::u16string& keys : typed) {
        type_sentence(keys, false);
    }
    while (!typed.empty() && measures[0].samples_ms.size() < min_samples) {
        for (const std::u16string& keys : typed) {
            type_sentence(keys, true);
        }
    }

    repeat(tens, [&](const std::u16string& reading) { converter.Convert(reading); }, measures[1]);
    repeat(thirties, [&](const std::u16string& reading) { converter.Convert(reading); }, measures[2]);
    std::vector<std::u16string> prefixes;
    for (const std::u16string& reading : readings) {
        for (std::size_t length = 2; length <= std::min<std::size_t>(6, reading.size()); ++length) {
            prefixes.push_back(reading.substr(0, length));
        }
    }
    repeat(prefixes, [&](const std::u16string& prefix) { converter.Predict(prefix, 9); }, measures[3]);
    repeat(tens, [&](const std::u16string& reading) { converter_with_user.Convert(reading); }, measures[4]);

    bool passed = true;
    std::string report = "## Performance of the core (test plan 5)\n\n"
                         "| ID | Measure | Samples | p50 (ms) | p95 (ms) | p99 (ms) | Target p95 (ms) | Result |\n"
                         "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const Measure& measure : measures) {
        const bool ok = measure.Passed() && !measure.samples_ms.empty();
        passed = passed && ok;
        std::cout << measure.id << ' ' << measure.name << ": samples=" << measure.samples_ms.size()
                  << " p50=" << Format(measure.Percentile(50)) << " p95=" << Format(measure.Percentile(95))
                  << " p99=" << Format(measure.Percentile(99)) << " target=" << measure.target_ms
                  << (ok ? " ok" : " OVER") << '\n';
        report += "| " + measure.id + " | " + measure.name + " | " + std::to_string(measure.samples_ms.size()) +
                  " | " + Format(measure.Percentile(50)) + " | " + Format(measure.Percentile(95)) + " | " +
                  Format(measure.Percentile(99)) + " | " + Format(measure.target_ms) + " | " +
                  (ok ? "ok" : "over") + " |\n";
    }
    report += "\nTimed on the CI runner (shared, so values vary between runs). P-01 covers the core only; the TIP "
              "and the app add their share on a real device.\n";
    if (!report_path.empty()) {
        std::ofstream(report_path, std::ios::app) << report;
    }
    return passed ? 0 : 1;
}
