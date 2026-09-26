#pragma once

#include "astelio/user_dictionary.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

// D-03: the user dictionary files of other IMEs, and our own JSON for moving words between PCs.
enum class UserDictionaryFormat : std::uint8_t {
    GoogleIme,   // Google Japanese Input / Mozc: tab separated text
    MsIme,       // Microsoft IME: text export
    Atok,        // ATOK: text export
    Kotoeri,     // macOS Japanese input: text export
    AstelioJson, // {"version": 1, "words": [{"reading", "surface", "pos", "comment"}]}
};

enum class TextEncoding : std::uint8_t { Utf8, Utf16Le, Utf16Be, ShiftJis };

// From the BOM; without one, UTF-8 when the bytes are valid UTF-8 (or plausible UTF-16 by its zero bytes),
// else Shift_JIS.
TextEncoding DetectEncoding(std::string_view bytes);
// Decodes UTF-8 and UTF-16 and drops the BOM. Shift_JIS is left to the platform (Windows: code page 932):
// nullopt for it and for malformed text.
std::optional<std::u16string> DecodeText(std::string_view bytes);
// The bytes of `text`, with a BOM for UTF-16. nullopt for Shift_JIS.
std::optional<std::string> EncodeText(std::u16string_view text, TextEncoding encoding);

struct UserDictionaryImport {
    std::optional<UserDictionaryFormat> format; // nullopt: not recognized, and nothing is imported
    std::vector<UserDictionary::Word> words;    // Valid words only, in file order
    std::size_t skipped = 0;                    // lines that held no importable word (headers and comments excluded)
};

// Recognizes the format of `text` and reads its words; parts of speech map as in the test plan (T-D03-1).
UserDictionaryImport ImportUserDictionary(std::u16string_view text);
// The text of `words` in `format`.
std::u16string ExportUserDictionary(const std::vector<UserDictionary::Word>& words, UserDictionaryFormat format);
// The encoding the IME of `format` reads.
TextEncoding ExportEncoding(UserDictionaryFormat format);

} // namespace astelio
