#pragma once

#include "astelio/romaji_table.h"

#include <string>
#include <string_view>

namespace astelio {

// Forms of a reading for F6-F10 (B-05).
std::u16string ToHalfWidthKatakana(std::u16string_view text); // hiragana and katakana -> ﾊﾝｶｸ
std::u16string ToFullWidthAscii(std::u16string_view text);    // ASCII -> ＡＳＣＩＩ
// Hepburn-style romaji that types back to the same kana with `table` ("しんぶん" -> "shinbun").
std::u16string KanaToRomaji(std::u16string_view text, const RomajiTable& table);

} // namespace astelio
