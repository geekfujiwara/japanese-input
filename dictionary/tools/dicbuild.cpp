// Dictionary builder: azooKey_dictionary_storage -> Astelio TSV sources -> binary system dictionary.
#include "astelio/dictionary.h"
#include "astelio/dictionary_builder.h"
#include "astelio/utf.h"
#include "azookey_import.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::optional<std::vector<std::byte>> ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

std::optional<std::string> ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool Fits(const std::u16string& text)
{
    return !text.empty() && text.size() <= 255 &&
           std::none_of(text.begin(), text.end(), [](char16_t c) { return c < 0x20 || c == 0x7F; });
}

int ImportAzooKey(const fs::path& root, const fs::path& words_path, const fs::path& connection_path)
{
    std::vector<fs::path> files;
    for (const fs::directory_entry& item : fs::directory_iterator(root / "louds")) {
        if (item.path().extension() == ".loudstxt3") {
            files.push_back(item.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no .loudstxt3 files under " << (root / "louds").string() << '\n';
        return 1;
    }

    std::ofstream words(words_path, std::ios::binary);
    words << "# reading\tsurface\tleft_id\tright_id\tmeaning_id\tcost (from azooKey_dictionary_storage, Apache-2.0)\n";
    std::size_t written = 0;
    std::size_t skipped = 0;
    for (const fs::path& path : files) {
        const std::optional<std::vector<std::byte>> bytes = ReadFile(path);
        std::vector<astelio::azookey::Entry> entries;
        if (!bytes || !astelio::azookey::ParseLoudsText(*bytes, entries)) {
            std::cerr << "malformed: " << path.filename().string() << '\n';
            return 1;
        }
        for (const astelio::azookey::Entry& entry : entries) {
            const std::u16string reading = astelio::azookey::KatakanaToHiragana(entry.reading);
            if (!Fits(reading) || !Fits(entry.surface) || entry.left_id >= astelio::azookey::kIdCount ||
                entry.right_id >= astelio::azookey::kIdCount) {
                ++skipped;
                continue;
            }
            words << astelio::Utf16ToUtf8(reading) << '\t' << astelio::Utf16ToUtf8(entry.surface) << '\t'
                  << entry.left_id << '\t' << entry.right_id << '\t' << entry.meaning_id << '\t'
                  << astelio::azookey::ToCost(entry.value) << '\n';
            ++written;
        }
    }

    std::ofstream connection(connection_path, std::ios::binary);
    connection << "# part-of-speech connection costs (from azooKey_dictionary_storage, Apache-2.0)\n";
    connection << "size\t" << astelio::azookey::kIdCount << '\t' << astelio::azookey::kBosId << '\t'
               << astelio::azookey::kEosId << '\n';
    std::size_t missing_rows = 0;
    for (std::uint16_t right = 0; right < astelio::azookey::kIdCount; ++right) {
        const std::optional<std::vector<std::byte>> bytes =
            ReadFile(root / "cb" / (std::to_string(right) + ".binary"));
        std::optional<std::vector<std::pair<std::int32_t, float>>> row;
        if (bytes) {
            row = astelio::azookey::ParseConnectionRow(*bytes);
        }
        if (!row) {
            ++missing_rows;
            connection << right << "\t*\t" << astelio::azookey::ToCost(astelio::azookey::kMissingRowValue) << '\n';
            continue;
        }
        connection << right << "\t*\t" << astelio::azookey::ToCost(row->front().second) << '\n';
        for (auto cell = row->begin() + 1; cell != row->end(); ++cell) {
            if (cell->first >= 0 && cell->first < astelio::azookey::kIdCount) {
                connection << right << '\t' << cell->first << '\t' << astelio::azookey::ToCost(cell->second) << '\n';
            }
        }
    }
    if (!words || !connection) {
        std::cerr << "failed to write the sources\n";
        return 1;
    }
    std::cout << "files=" << files.size() << " entries=" << written << " skipped=" << skipped
              << " missing_rows=" << missing_rows << '\n';
    return 0;
}

int Build(const fs::path& connection_path, const fs::path& output, const std::vector<fs::path>& word_paths)
{
    const std::optional<std::string> connection_text = ReadText(connection_path);
    if (!connection_text) {
        std::cerr << "cannot read " << connection_path.string() << '\n';
        return 1;
    }
    astelio::SourceError error;
    std::optional<astelio::ConnectionMatrix> matrix = astelio::ParseConnectionSource(*connection_text, &error);
    if (!matrix) {
        std::cerr << connection_path.filename().string() << ':' << error.line << ": " << error.message << '\n';
        return 1;
    }
    const std::uint16_t id_count = matrix->size;
    astelio::DictionaryBuilder builder(std::move(*matrix));
    for (const fs::path& path : word_paths) {
        const std::optional<std::string> text = ReadText(path);
        if (!text) {
            std::cerr << "cannot read " << path.string() << '\n';
            return 1;
        }
        std::vector<astelio::SourceError> errors;
        std::vector<astelio::DictionarySourceEntry> entries = astelio::ParseWordSource(*text, id_count, errors);
        for (std::size_t i = 0; i < errors.size() && i < 10; ++i) {
            std::cerr << path.filename().string() << ':' << errors[i].line << ": " << errors[i].message << '\n';
        }
        if (!errors.empty()) {
            std::cerr << errors.size() << " bad lines in " << path.filename().string() << '\n';
            return 1;
        }
        for (astelio::DictionarySourceEntry& entry : entries) {
            builder.Add(std::move(entry));
        }
    }
    const std::vector<std::byte> bytes = builder.Build();
    const std::optional<astelio::SystemDictionary> check = astelio::SystemDictionary::Open(bytes);
    if (!check) {
        std::cerr << "the built dictionary does not validate\n";
        return 1;
    }
    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "cannot write " << output.string() << '\n';
        return 1;
    }
    std::cout << "readings=" << check->reading_count() << " entries=" << check->entry_count()
              << " bytes=" << bytes.size() << '\n';
    return 0;
}

// Prints the entries for each reading; fails when one has none (used as a smoke test in CI).
int Lookup(const fs::path& path, const std::vector<std::string>& readings)
{
    const auto started = std::chrono::steady_clock::now();
    const std::optional<std::vector<std::byte>> bytes = ReadFile(path);
    if (!bytes) {
        std::cerr << "cannot read " << path.string() << '\n';
        return 1;
    }
    astelio::DictionaryError error{};
    const std::optional<astelio::SystemDictionary> dictionary = astelio::SystemDictionary::Open(*bytes, &error);
    const auto opened = std::chrono::steady_clock::now();
    if (!dictionary) {
        std::cerr << "invalid dictionary (error " << static_cast<int>(error) << ")\n";
        return 1;
    }
    std::cout << "load_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(opened - started).count() << '\n';
    int status = 0;
    for (const std::string& reading : readings) {
        const std::optional<std::u16string> query = astelio::Utf8ToUtf16(reading);
        const std::vector<astelio::DictionaryEntry> entries =
            query ? dictionary->Lookup(*query) : std::vector<astelio::DictionaryEntry>{};
        std::cout << reading << ':';
        for (std::size_t i = 0; i < entries.size() && i < 10; ++i) {
            std::cout << ' ' << astelio::Utf16ToUtf8(entries[i].surface) << '(' << entries[i].cost << ')';
        }
        std::cout << '\n';
        if (entries.empty()) {
            status = 1;
        }
    }
    return status;
}

int Usage()
{
    std::cerr << "usage:\n"
                 "  astelio_dicbuild import-azookey <azooKey Dictionary dir> <words.tsv> <connection.tsv>\n"
                 "  astelio_dicbuild build <connection.tsv> <system.dic> <words.tsv>...\n"
                 "  astelio_dicbuild lookup <system.dic> <reading>...\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    try {
        if (args.size() == 4 && args[0] == "import-azookey") {
            return ImportAzooKey(args[1], args[2], args[3]);
        }
        if (args.size() >= 4 && args[0] == "build") {
            return Build(args[1], args[2], std::vector<fs::path>(args.begin() + 3, args.end()));
        }
        if (args.size() >= 3 && args[0] == "lookup") {
            return Lookup(args[1], std::vector<std::string>(args.begin() + 2, args.end()));
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return Usage();
}
