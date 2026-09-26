// B-14 もしかして on eval/typo/typo.tsv (test plan T-B14-4): how often the suggestion is the word meant, how often a
// correct reading gets a suggestion, and how long it takes. Also writes the candidates as JSON for slm/evaluate.py
// (T-S06), and fails when the scores fall behind the baseline.
#include "astelio/composer.h"
#include "astelio/dictionary.h"
#include "astelio/kana_forms.h"
#include "astelio/romaji_table.h"
#include "astelio/typo_candidates.h"
#include "astelio/utf.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::optional<std::string> ReadText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::vector<std::string> Split(const std::string& line, char separator)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = line.find(separator, begin);
        fields.push_back(line.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            return fields;
        }
        begin = end + 1;
    }
}

std::string Json(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += ' ';
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

std::string Json(const std::u16string& text)
{
    return Json(astelio::Utf16ToUtf8(text));
}

std::string Utf8(const std::u16string& text)
{
    return astelio::Utf16ToUtf8(text);
}

std::u16string ToKana(const std::u16string& keys)
{
    astelio::Composer composer(astelio::RomajiTable::Default(), astelio::CharacterSettings{});
    for (const char16_t key : keys) {
        composer.InsertKey(key);
    }
    return composer.Commit();
}

bool IsHiraganaWord(std::u16string_view text)
{
    return std::all_of(text.begin(), text.end(),
                       [](char16_t c) { return (c >= u'\u3041' && c <= u'\u3094') || c == u'\u30FC'; });
}

std::string Percent(std::size_t part, std::size_t whole)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / whole);
    return text;
}

double Rate(std::size_t part, std::size_t whole)
{
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / whole;
}

long long Percentile(std::vector<long long> values, std::size_t percent)
{
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, values.size() * percent / 100)];
}

// "name<TAB>value" lines; '#' starts a comment.
std::map<std::string, double> ReadBaseline(const std::string& path)
{
    std::map<std::string, double> values;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const std::vector<std::string> fields = Split(line, '\t');
        if (line.empty() || line.front() == '#' || fields.size() != 2) {
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(fields[1].c_str(), &end);
        if (end != fields[1].c_str()) {
            values[fields[0]] = value;
        }
    }
    return values;
}

int Usage()
{
    std::cerr << "usage: astelio_typo <system.dic> <typo.tsv> [--json file] [--report file] [--baseline file]"
                 " [--frequent count]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        return Usage();
    }
    std::string json_path;
    std::string report_path;
    std::string baseline_path;
    std::size_t frequent_count = 0;
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        if (i + 1 >= argc) {
            return Usage();
        }
        const std::string value = argv[++i];
        if (option == "--json") {
            json_path = value;
        } else if (option == "--report") {
            report_path = value;
        } else if (option == "--baseline") {
            baseline_path = value;
        } else if (option == "--frequent") {
            frequent_count = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else {
            return Usage();
        }
    }

    const std::optional<std::string> dictionary_text = ReadText(argv[1]);
    std::vector<std::byte> bytes;
    if (dictionary_text) {
        bytes.resize(dictionary_text->size());
        std::memcpy(bytes.data(), dictionary_text->data(), bytes.size());
    }
    const std::optional<astelio::SystemDictionary> dictionary = astelio::SystemDictionary::Open(bytes);
    const std::optional<std::string> corpus = ReadText(argv[2]);
    if (!dictionary || !corpus) {
        std::cerr << "cannot read the dictionary or the corpus\n";
        return 1;
    }
    const astelio::RomajiTable& table = astelio::RomajiTable::Default();

    // The word before: the right id of the cheapest entry whose surface ends the context.
    constexpr std::size_t kLongestContextWord = 8;
    std::unordered_map<std::u16string, std::pair<std::int16_t, std::uint16_t>> by_surface; // cost, right id
    struct Frequent {
        std::u16string reading;
        std::u16string surface;
        std::int16_t cost;
    };
    std::vector<Frequent> frequent;
    dictionary->ForEachEntry([&](std::u16string_view reading, const astelio::DictionaryEntry& entry) {
        if (entry.surface.size() <= kLongestContextWord) {
            const auto [found, inserted] =
                by_surface.emplace(std::u16string(entry.surface), std::pair(entry.cost, entry.right_id));
            if (!inserted && entry.cost < found->second.first) {
                found->second = {entry.cost, entry.right_id};
            }
        }
        if (frequent_count > 0 && reading.size() >= 2 && reading.size() <= 8 && IsHiraganaWord(reading) &&
            !IsHiraganaWord(entry.surface.substr(entry.surface.size() - 1)) &&
            dictionary->word_type(entry.left_id) == astelio::WordType::Content) {
            frequent.push_back({std::u16string(reading), std::u16string(entry.surface), entry.cost});
        }
    });
    const auto previous_of = [&](const std::u16string& context) -> std::optional<std::uint16_t> {
        for (std::size_t length = std::min(context.size(), kLongestContextWord); length > 0; --length) {
            const auto found = by_surface.find(context.substr(context.size() - length));
            if (found != by_surface.end()) {
                return found->second.second;
            }
        }
        return std::nullopt;
    };

    std::string json = "{\"entries\": [\n";
    // The same scores with other margins, to choose kTypoSuggestMargin.
    constexpr std::int32_t kMargins[] = {0, 250, 500, 750, 1000, 1500};
    constexpr std::size_t kMarginCount = std::size(kMargins);
    std::size_t sweep_hits[kMarginCount] = {};
    std::size_t sweep_false[kMarginCount] = {};
    std::size_t sweep_frequent_false[kMarginCount] = {};
    std::vector<long long> micros;
    std::size_t typos = 0;
    std::size_t hits = 0;
    std::size_t reachable = 0;
    std::size_t corrects = 0;
    std::size_t false_suggestions = 0;
    std::ostringstream misses;
    std::size_t number = 0;
    std::size_t count = 0;
    int status = 0;
    for (std::string line : Split(*corpus, '\n')) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> fields = Split(line, '\t');
        const std::optional<std::u16string> context = fields.size() >= 5 ? astelio::Utf8ToUtf16(fields[1]) : std::nullopt;
        const std::optional<std::u16string> given_keys = fields.size() >= 5 ? astelio::Utf8ToUtf16(fields[2]) : std::nullopt;
        const std::optional<std::u16string> given_reading = fields.size() >= 5 ? astelio::Utf8ToUtf16(fields[3]) : std::nullopt;
        if (!context || !given_keys || !given_reading || (given_keys->empty() && given_reading->empty()) ||
            fields[4].empty() || (fields[0] != "typo" && fields[0] != "correct")) {
            std::cerr << "typo.tsv:" << number << ": expected kind, context, keys, reading, intended text\n";
            status = 1;
            continue;
        }
        const std::u16string keys = given_keys->empty() ? astelio::KanaToRomaji(*given_reading, table) : *given_keys;
        const std::u16string reading = ToKana(keys);
        if (!given_reading->empty() && reading != *given_reading) {
            std::cerr << "typo.tsv:" << number << ": the keys type " << Utf8(reading) << ", not " << fields[3] << '\n';
            status = 1;
            continue;
        }
        const std::vector<std::string> expected = Split(fields[4], '|');
        const auto is_expected = [&expected](const std::u16string& surface) {
            return std::find(expected.begin(), expected.end(), Utf8(surface)) != expected.end();
        };
        const std::optional<std::uint16_t> previous = previous_of(*context);

        const auto started = std::chrono::steady_clock::now();
        const std::optional<astelio::TypoCandidate> suggestion =
            astelio::SuggestTypoCorrection(keys, table, *dictionary, previous);
        micros.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        const std::vector<astelio::TypoCandidate> candidates = astelio::FindTypoCandidates(keys, table, *dictionary, 20);
        for (std::size_t m = 0; m < kMarginCount; ++m) {
            const std::optional<astelio::TypoCandidate> other =
                astelio::SuggestTypoCorrection(keys, table, *dictionary, previous, kMargins[m]);
            if (fields[0] == "typo" && other && is_expected(other->surface)) {
                ++sweep_hits[m];
            } else if (fields[0] == "correct" && other) {
                ++sweep_false[m];
            }
        }

        const std::vector<astelio::DictionaryEntry> typed = dictionary->Lookup(reading);
        const std::string typed_text = Utf8(typed.empty() ? reading : std::u16string(typed.front().surface));
        const std::string typed_cost = typed.empty() ? std::string("—") : std::to_string(typed.front().cost);
        const std::string suggested_text = suggestion ? Utf8(suggestion->surface) : std::string("（なし）");
        if (fields[0] == "typo") {
            ++typos;
            const auto rank = std::find_if(candidates.begin(), candidates.end(),
                                           [&](const astelio::TypoCandidate& c) { return is_expected(c.surface); });
            if (rank != candidates.end()) {
                ++reachable;
            }
            if (suggestion && is_expected(suggestion->surface)) {
                ++hits;
            } else {
                misses << "| " << number << " | typo | " << fields[1] << " | `" << Utf8(keys) << "` " << Utf8(reading)
                       << " | " << fields[4] << " | " << suggested_text << " | " << typed_text << " | "
                       << (rank == candidates.end() ? std::string("—") : std::to_string(rank - candidates.begin() + 1))
                       << " | " << typed_cost << " / "
                       << (rank == candidates.end() ? std::string("—") : std::to_string(rank->cost)) << " / "
                       << (candidates.empty() ? std::string("—") : Utf8(candidates.front().surface) + " " +
                                                                        std::to_string(candidates.front().cost))
                       << " |\n";
            }
        } else {
            ++corrects;
            if (suggestion) {
                ++false_suggestions;
                misses << "| " << number << " | correct | " << fields[1] << " | `" << Utf8(keys) << "` "
                       << Utf8(reading) << " | " << fields[4] << " | " << suggested_text << " | " << typed_text
                       << " | — | " << typed_cost << " / — / " << Utf8(suggestion->surface) << ' ' << suggestion->cost
                       << " |\n";
            }
        }

        const std::vector<astelio::TypoCandidate> listed(candidates.begin(),
                                                         candidates.begin() + std::min<std::size_t>(5, candidates.size()));
        json += count++ == 0 ? "" : ",\n";
        json += "{\"kind\": " + Json(fields[0]) + ", \"context\": " + Json(fields[1]) + ", \"keys\": " + Json(keys) +
                ", \"reading\": " + Json(reading) + ", \"expected\": " + Json(expected.front()) +
                ", \"typed_surface\": " + Json(typed_text) +
                ", \"typed_cost\": " + (typed.empty() ? std::string("null") : std::to_string(typed.front().cost)) +
                ", \"candidates\": [";
        for (std::size_t i = 0; i < listed.size(); ++i) {
            json += (i == 0 ? "" : ", ") + std::string("{\"surface\": ") + Json(listed[i].surface) +
                    ", \"reading\": " + Json(listed[i].reading) + ", \"keys\": " + Json(listed[i].keys) +
                    ", \"cost\": " + std::to_string(listed[i].cost) + "}";
        }
        json += "]}";
    }

    // Common words typed correctly should not get a suggestion either.
    std::sort(frequent.begin(), frequent.end(), [](const Frequent& a, const Frequent& b) {
        return a.cost != b.cost ? a.cost < b.cost : a.reading < b.reading;
    });
    std::size_t frequent_checked = 0;
    std::size_t frequent_false = 0;
    std::ostringstream frequent_misses;
    std::vector<std::u16string> seen;
    for (const Frequent& word : frequent) {
        if (frequent_checked >= frequent_count) {
            break;
        }
        if (std::find(seen.begin(), seen.end(), word.reading) != seen.end()) {
            continue;
        }
        seen.push_back(word.reading);
        const std::u16string keys = astelio::KanaToRomaji(word.reading, table);
        if (ToKana(keys) != word.reading) {
            continue;
        }
        ++frequent_checked;
        for (std::size_t m = 0; m < kMarginCount; ++m) {
            if (astelio::SuggestTypoCorrection(keys, table, *dictionary, std::nullopt, kMargins[m])) {
                ++sweep_frequent_false[m];
            }
        }
        const std::optional<astelio::TypoCandidate> suggestion =
            astelio::SuggestTypoCorrection(keys, table, *dictionary);
        if (suggestion) {
            ++frequent_false;
            if (frequent_false <= 30) {
                frequent_misses << "| `" << Utf8(keys) << "` " << Utf8(word.reading) << " | " << Utf8(word.surface)
                                << ' ' << word.cost << " | " << Utf8(suggestion->surface) << ' ' << suggestion->cost
                                << " |\n";
            }
        }
    }

    const long long p50 = Percentile(micros, 50);
    const long long p95 = Percentile(micros, 95);
    const double hit_rate = Rate(hits, typos);
    const double false_rate = Rate(false_suggestions, corrects);
    const double frequent_rate = Rate(frequent_false, frequent_checked);

    std::ostringstream report;
    report << "## もしかして（B-14）\n\n"
           << "| 項目 | 値 |\n| --- | ---: |\n"
           << "| 打ち間違いの的中率 | " << Percent(hits, typos) << "% (" << hits << "/" << typos << ") |\n"
           << "| 候補に正解が入る割合 | " << Percent(reachable, typos) << "% (" << reachable << "/" << typos << ") |\n"
           << "| 正しい読みへの誤提案率 | " << Percent(false_suggestions, corrects) << "% (" << false_suggestions << "/"
           << corrects << ") |\n"
           << "| よく使う語への誤提案率 | " << Percent(frequent_false, frequent_checked) << "% (" << frequent_false << "/"
           << frequent_checked << ") |\n"
           << "| 提案の時間 p50 / p95 | " << p50 << " / " << p95 << " µs |\n\n";
    report << "<details><summary>判定の余裕（打った語があるとき）ごとの比較（現在 " << astelio::kTypoSuggestMargin
           << "）</summary>\n\n| 余裕 | 的中率 | 正しい読みへの誤提案率 | よく使う語への誤提案率 |\n| ---: | ---: | ---: | ---: |\n";
    for (std::size_t m = 0; m < kMarginCount; ++m) {
        report << "| " << kMargins[m] << " | " << Percent(sweep_hits[m], typos) << "% | "
               << Percent(sweep_false[m], corrects) << "% | " << Percent(sweep_frequent_false[m], frequent_checked)
               << "% |\n";
    }
    report << "\n</details>\n\n";
    if (!misses.str().empty()) {
        report << "<details><summary>外した例</summary>\n\n"
               << "| 行 | 種類 | 前の文脈 | 打ったキー | 意図 | 提案 | 打った語 | 候補での順位 | コスト（打った語 / 正解 / 先頭） |\n"
               << "| ---: | --- | --- | --- | --- | --- | --- | ---: | --- |\n"
               << misses.str() << "\n</details>\n\n";
    }
    if (!frequent_misses.str().empty()) {
        report << "<details><summary>よく使う語への誤提案（最大30件）</summary>\n\n"
               << "| 打ったキー | 語 | 提案 |\n| --- | --- | --- |\n"
               << frequent_misses.str() << "\n</details>\n\n";
    }

    if (!baseline_path.empty()) {
        const std::map<std::string, double> baseline = ReadBaseline(baseline_path);
        const auto check = [&](const char* name, double value, bool higher_is_better) {
            const auto found = baseline.find(name);
            if (found == baseline.end()) {
                std::cerr << "the baseline has no " << name << '\n';
                status = 1;
                return;
            }
            const bool worse = higher_is_better ? value <= found->second - 0.5 : value >= found->second + 0.5;
            if (worse) {
                report << "**" << name << " が基準値 " << found->second << " から " << value << " に悪化した**\n\n";
                status = 1;
            }
        };
        check("typo_hit_rate", hit_rate, true);
        check("false_suggestion_rate", false_rate, false);
        check("frequent_false_rate", frequent_rate, false);
    }

    std::cout << report.str();
    if (!report_path.empty()) {
        std::ofstream(report_path, std::ios::binary | std::ios::app) << report.str();
    }
    if (!json_path.empty()) {
        json += "\n], \"candidate_p50_us\": " + std::to_string(p50) + ", \"candidate_p95_us\": " + std::to_string(p95) +
                "}\n";
        std::ofstream(json_path, std::ios::binary) << json;
    }
    return status;
}
