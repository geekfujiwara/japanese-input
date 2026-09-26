#pragma once

#include "astelio/user_dictionary.h"
#include "astelio/user_dictionary_io.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio::tip {

// D-02: the user's words, in %APPDATA%\AstelioIME\user_dictionary.tsv. Shared by every app like the learning
// history: written whole through a temporary file, and reloaded when another app changed it.

// Tests use their own file; nullptr goes back to the default.
void UseUserDictionaryFile(const wchar_t* path);
UserDictionary LoadUserDictionary();
bool SaveUserDictionary(const UserDictionary& dictionary);
// Changes when the file is written or removed (0 when it is missing), to notice changes made by other apps.
std::uint64_t UserDictionaryFileStamp();

// D-03: the text of a file: Shift_JIS through code page 932, the rest by DecodeText. nullopt when malformed.
std::optional<std::u16string> DecodeUserDictionaryFile(std::string_view bytes);
// nullopt when a character has no Shift_JIS code.
std::optional<std::string> EncodeUserDictionaryFile(std::u16string_view text, TextEncoding encoding);

struct UserDictionaryFileImport {
    bool read = false;                          // the file was opened and its text decoded
    std::optional<UserDictionaryFormat> format; // nullopt: not recognized, and nothing was added
    std::size_t added = 0;
    std::size_t skipped = 0; // lines without a word, and words the dictionary had no room for
};

// Adds the words of the file at `path` to `dictionary`; a file that cannot be read leaves it as it is (F-04).
UserDictionaryFileImport ImportUserDictionaryFile(const std::wstring& path, UserDictionary& dictionary);

enum class UserDictionaryExport { Written, NotEncodable, NotWritten };

// Writes `words` in `format`, in the encoding that format's IME reads.
UserDictionaryExport ExportUserDictionaryFile(const std::wstring& path, const std::vector<UserDictionary::Word>& words,
                                              UserDictionaryFormat format);

} // namespace astelio::tip
