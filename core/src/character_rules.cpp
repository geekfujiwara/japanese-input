#include "astelio/character_rules.h"

#include <cstddef>
#include <string_view>

namespace astelio {
namespace {

bool IsDigit(char16_t c) { return c >= u'0' && c <= u'9'; }

bool IsLetter(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

bool IsPrintableAscii(char16_t c) { return c >= 0x21 && c <= 0x7E; }

char16_t JapaneseForm(char16_t ascii)
{
    switch (ascii) {
    case u'[': return u'「';
    case u']': return u'」';
    case u'{': return u'『';
    case u'}': return u'』';
    case u'/': return u'・';
    case u'-': return u'ー';
    case u'~': return u'〜';
    case u'!': return u'！';
    case u'?': return u'？';
    default: return 0;
    }
}

char16_t HalfWidthJapaneseSymbol(char16_t c)
{
    switch (c) {
    case u'「': return u'｢';
    case u'」': return u'｣';
    case u'、': return u'､';
    case u'。': return u'｡';
    case u'・': return u'･';
    case u'！': return u'!';
    case u'？': return u'?';
    default: return c;
    }
}

std::u16string JapaneseSymbol(char16_t c, const CharacterSettings& settings)
{
    return std::u16string(1, settings.half_width_japanese_symbols ? HalfWidthJapaneseSymbol(c) : c);
}

std::u16string Punctuation(char16_t ascii, const CharacterSettings& settings)
{
    const bool comma = ascii == u',';
    switch (settings.punctuation) {
    case PunctuationStyle::ToutenKuten:
        return JapaneseSymbol(comma ? u'、' : u'。', settings);
    case PunctuationStyle::FullCommaPeriod:
        return comma ? u"，" : u"．";
    case PunctuationStyle::HalfCommaPeriod:
        return comma ? u"," : u".";
    case PunctuationStyle::ToutenFullPeriod:
        return comma ? JapaneseSymbol(u'、', settings) : std::u16string(u"．");
    }
    return std::u16string(1, ascii);
}

} // namespace

std::array<SymbolForm, 128> CharacterSettings::DefaultSymbolForms()
{
    std::array<SymbolForm, 128> forms{};
    forms.fill(SymbolForm::Half);
    for (char16_t c : std::u16string_view{u"[]/-!?"}) {
        forms[c] = SymbolForm::Japanese;
    }
    return forms;
}

std::u16string ToWidth(char16_t ascii, Width width)
{
    if (width == Width::Half) {
        return std::u16string(1, ascii);
    }
    if (ascii == u' ') {
        return u"\u3000";
    }
    if (IsPrintableAscii(ascii)) {
        return std::u16string(1, static_cast<char16_t>(ascii - 0x21 + 0xFF01));
    }
    return std::u16string(1, ascii);
}

std::u16string KanaModeCharacter(char16_t ascii, const CharacterSettings& settings)
{
    if (IsDigit(ascii)) {
        return ToWidth(ascii, settings.digits);
    }
    if (IsLetter(ascii)) {
        return ToWidth(ascii, settings.letters);
    }
    if (ascii == u',' || ascii == u'.') {
        return Punctuation(ascii, settings);
    }
    if (static_cast<std::size_t>(ascii) >= settings.symbols.size()) {
        return std::u16string(1, ascii);
    }
    switch (settings.symbols[ascii]) {
    case SymbolForm::Half:
        return ToWidth(ascii, Width::Half);
    case SymbolForm::Full:
        return ToWidth(ascii, Width::Full);
    case SymbolForm::Japanese:
        if (char16_t japanese = JapaneseForm(ascii); japanese != 0) {
            return JapaneseSymbol(japanese, settings);
        }
        return ToWidth(ascii, Width::Full);
    }
    return std::u16string(1, ascii);
}

std::u16string AlphanumericModeCharacter(char16_t ascii, const CharacterSettings& settings)
{
    return ToWidth(ascii, IsDigit(ascii) ? settings.digits : settings.letters);
}

std::u16string SpaceCharacter(const CharacterSettings& settings)
{
    return ToWidth(u' ', settings.space);
}

} // namespace astelio
