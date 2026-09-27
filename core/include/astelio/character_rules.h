#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace astelio {

enum class Width : std::uint8_t {
    Half,
    Full,
};

// How an ASCII symbol key is written while composing Japanese text.
enum class SymbolForm : std::uint8_t {
    Half,     // "["
    Full,     // "［"
    Japanese, // "「"
};

enum class PunctuationStyle : std::uint8_t {
    ToutenKuten,      // 、。
    FullCommaPeriod,  // ，．
    HalfCommaPeriod,  // ,.
    ToutenFullPeriod, // 、．
};

struct CharacterSettings {
    Width letters = Width::Half;
    Width digits = Width::Half;
    Width space = Width::Half;
    PunctuationStyle punctuation = PunctuationStyle::ToutenKuten;
    // R-05: write 「」、。・！？ as ｢｣､｡･!? instead.
    bool half_width_japanese_symbols = false;
    // R-10: '.' right after a number that starts the text is ". " (a list number), not 。.
    bool list_number_period = true;
    // R-11: an opening bracket key also writes its closing bracket, with the cursor between them.
    bool auto_close_brackets = true;
    // Indexed by ASCII code. Only printable symbols (not letters, digits, ',' or '.') are used.
    std::array<SymbolForm, 128> symbols = DefaultSymbolForms();

    static std::array<SymbolForm, 128> DefaultSymbolForms();
};

std::u16string ToWidth(char16_t ascii, Width width);

// Output for a digit, symbol, or punctuation key typed in kana mode.
std::u16string KanaModeCharacter(char16_t ascii, const CharacterSettings& settings);

// Output for any printable ASCII key typed in temporary alphanumeric mode.
std::u16string AlphanumericModeCharacter(char16_t ascii, const CharacterSettings& settings);

std::u16string SpaceCharacter(const CharacterSettings& settings);

// R-11: the closing bracket of `opening` (「 『 （ ( ［ [ ｛ { ｢), or 0.
char16_t ClosingBracket(char16_t opening);
bool IsClosingBracket(char16_t c);

} // namespace astelio
