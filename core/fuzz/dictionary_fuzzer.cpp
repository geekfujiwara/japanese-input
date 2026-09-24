// F-02: libFuzzer entry for the system dictionary reader and the dictionary sources.
#include "astelio/dictionary.h"
#include "astelio/dictionary_builder.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

void Check(bool condition)
{
    if (!condition) {
        __builtin_trap();
    }
}

const std::vector<std::byte>& ValidDictionary()
{
    static const std::vector<std::byte> bytes = [] {
        astelio::DictionaryBuilder builder(astelio::ConnectionMatrix{3, 0, 2, std::vector<std::int16_t>(9, 10)});
        builder.Add({u"\u308F\u305F\u3057", u"\u79C1", 1, 1, 0, 100});
        builder.Add({u"\u308F", u"\u8F2A", 1, 1, 0, 200});
        builder.Add({u"\u306F", u"\u306F", 1, 1, 0, 50});
        return builder.Build();
    }();
    return bytes;
}

void Exercise(const astelio::SystemDictionary& dictionary, std::u16string_view query)
{
    std::size_t visited = 0;
    dictionary.CommonPrefixSearch(query, [&visited](std::size_t length, const astelio::DictionaryEntry& entry) {
        visited += length + entry.surface.size();
    });
    Check(dictionary.Lookup(query).size() <= dictionary.entry_count());
    static_cast<void>(dictionary.ConnectionCost(dictionary.bos_id(), dictionary.eos_id()));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    // Raw bytes as a dictionary file.
    std::vector<std::byte> raw(size);
    if (size > 0) {
        std::memcpy(raw.data(), data, size);
    }
    if (const auto dictionary = astelio::SystemDictionary::Open(raw)) {
        Exercise(*dictionary, u"\u308F\u305F\u3057");
    }

    // A valid dictionary with bytes overwritten at fuzzer-chosen positions (reaches the deeper checks).
    std::vector<std::byte> mutated = ValidDictionary();
    for (std::size_t i = 0; i + 2 < size; i += 3) {
        const std::size_t position = (std::size_t{data[i]} << 8 | data[i + 1]) % mutated.size();
        mutated[position] = static_cast<std::byte>(data[i + 2]);
    }
    std::u16string query;
    for (std::size_t i = 0; i + 1 < size && query.size() < 16; i += 2) {
        query.push_back(static_cast<char16_t>(0x3040 + (data[i] % 0x60)));
    }
    if (const auto dictionary = astelio::SystemDictionary::Open(mutated)) {
        Exercise(*dictionary, query);
    }

    const std::string_view text(reinterpret_cast<const char*>(data), size);
    astelio::SourceError error;
    static_cast<void>(astelio::ParseConnectionSource(text, &error));
    std::vector<astelio::SourceError> errors;
    for (const astelio::DictionarySourceEntry& entry : astelio::ParseWordSource(text, 16, errors)) {
        Check(!entry.reading.empty() && !entry.surface.empty() && entry.left_id < 16 && entry.right_id < 16);
    }
    return 0;
}
