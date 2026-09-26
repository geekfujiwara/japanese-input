// F-04: libFuzzer entry for the user dictionary import (encodings, the other IMEs' text and our JSON).
#include "astelio/user_dictionary.h"
#include "astelio/user_dictionary_io.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

void Check(bool condition)
{
    if (!condition) {
        __builtin_trap();
    }
}

constexpr astelio::UserDictionaryFormat kFormats[] = {
    astelio::UserDictionaryFormat::GoogleIme, astelio::UserDictionaryFormat::MsIme,
    astelio::UserDictionaryFormat::Atok,      astelio::UserDictionaryFormat::Kotoeri,
    astelio::UserDictionaryFormat::AstelioJson,
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);
    const auto encoding = astelio::DetectEncoding(bytes);
    const auto text = astelio::DecodeText(bytes);
    Check(!text || encoding != astelio::TextEncoding::ShiftJis);
    if (!text) {
        return 0;
    }

    const auto imported = astelio::ImportUserDictionary(*text);
    Check(imported.format.has_value() || imported.words.empty());
    for (const auto& word : imported.words) {
        Check(astelio::UserDictionary::Valid(word));
    }

    for (const auto format : kFormats) {
        const auto exported = astelio::ExportUserDictionary(imported.words, format);
        const auto again = astelio::ImportUserDictionary(exported);
        for (const auto& word : again.words) {
            Check(astelio::UserDictionary::Valid(word));
        }
        if (format == astelio::UserDictionaryFormat::AstelioJson) {
            Check(again.words == imported.words);
        }
        static_cast<void>(astelio::EncodeText(exported, astelio::ExportEncoding(format)));
    }
    return 0;
}
