// T-S06: typo candidates from the dictionary for every line of slm/corpus/typo.tsv, as JSON for slm/evaluate.py.
#include "astelio/composer.h"
#include "astelio/dictionary.h"
#include "astelio/kana_forms.h"
#include "astelio/romaji_table.h"
#include "astelio/typo_candidates.h"
#include "astelio/utf.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
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

std::u16string ToKana(const std::u16string& keys)
{
    astelio::Composer composer(astelio::RomajiTable::Default(), astelio::CharacterSettings{});
    for (const char16_t key : keys) {
        composer.InsertKey(key);
    }
    return composer.Commit();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: astelio_typo <system.dic> <typo.tsv> <candidates.json>\n";
        return 2;
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

    std::string json = "{\"entries\": [\n";
    std::vector<long long> micros;
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
        const std::optional<std::u16string> reading = fields.size() >= 5 ? astelio::Utf8ToUtf16(fields[3]) : std::nullopt;
        const std::optional<std::u16string> given_keys = fields.size() >= 5 ? astelio::Utf8ToUtf16(fields[2]) : std::nullopt;
        if (!reading || !given_keys || reading->empty() || (fields[0] != "typo" && fields[0] != "correct")) {
            std::cerr << "typo.tsv:" << number << ": expected kind, context, keys, reading, intended text\n";
            status = 1;
            continue;
        }
        const std::u16string keys = given_keys->empty() ? astelio::KanaToRomaji(*reading, table) : *given_keys;
        if (ToKana(keys) != *reading) {
            std::cerr << "typo.tsv:" << number << ": the keys do not type the reading\n";
            status = 1;
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        const std::vector<astelio::TypoCandidate> candidates = astelio::FindTypoCandidates(keys, table, *dictionary, 5);
        micros.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());

        const std::vector<astelio::DictionaryEntry> typed = dictionary->Lookup(*reading);
        json += count++ == 0 ? "" : ",\n";
        json += "{\"kind\": " + Json(fields[0]) + ", \"context\": " + Json(fields[1]) + ", \"keys\": " + Json(keys) +
                ", \"reading\": " + Json(*reading) + ", \"expected\": " + Json(fields[4]) +
                ", \"typed_surface\": " + Json(typed.empty() ? *reading : std::u16string(typed.front().surface)) +
                ", \"typed_cost\": " + (typed.empty() ? std::string("null") : std::to_string(typed.front().cost)) +
                ", \"candidates\": [";
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            json += (i == 0 ? "" : ", ") + std::string("{\"surface\": ") + Json(candidates[i].surface) +
                    ", \"reading\": " + Json(candidates[i].reading) + ", \"keys\": " + Json(candidates[i].keys) +
                    ", \"cost\": " + std::to_string(candidates[i].cost) + "}";
        }
        json += "]}";
    }
    std::sort(micros.begin(), micros.end());
    const long long p50 = micros.empty() ? 0 : micros[micros.size() / 2];
    const long long p95 = micros.empty() ? 0 : micros[std::min(micros.size() - 1, micros.size() * 95 / 100)];
    json += "\n], \"candidate_p50_us\": " + std::to_string(p50) + ", \"candidate_p95_us\": " + std::to_string(p95) + "}\n";
    std::ofstream(argv[3], std::ios::binary) << json;
    std::cout << "entries=" << count << " candidates_p50_us=" << p50 << " p95_us=" << p95 << '\n';
    return status;
}
