#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace astelio {

// Returns std::nullopt for malformed UTF-8 (overlong forms, surrogates, truncation).
std::optional<std::u16string> Utf8ToUtf16(std::string_view utf8);

// Unpaired surrogates are written as U+FFFD.
std::string Utf16ToUtf8(std::u16string_view utf16);

} // namespace astelio
