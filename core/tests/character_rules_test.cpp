#include "astelio/character_rules.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace astelio {
namespace {

constexpr std::u16string_view kSymbols = u"`~!@#$%^&*()-_=+[{]}\\|;:'\"<>/?";

// T-R05-2
TEST(CharacterRules, HalfWidthJapaneseSymbolsSetting)
{
    CharacterSettings settings;
    settings.half_width_japanese_symbols = true;
    EXPECT_EQ(KanaModeCharacter(u'[', settings), u"｢");
    EXPECT_EQ(KanaModeCharacter(u']', settings), u"｣");
    EXPECT_EQ(KanaModeCharacter(u'.', settings), u"｡");
    EXPECT_EQ(KanaModeCharacter(u',', settings), u"､");
    EXPECT_EQ(KanaModeCharacter(u'/', settings), u"･");
    EXPECT_EQ(KanaModeCharacter(u'!', settings), u"!");
    EXPECT_EQ(KanaModeCharacter(u'?', settings), u"?");
    EXPECT_EQ(KanaModeCharacter(u'-', settings), u"ー");
}

// T-C01-1
TEST(CharacterRules, EverySymbolFollowsItsConfiguredForm)
{
    for (char16_t key : kSymbols) {
        CharacterSettings settings;
        settings.symbols[key] = SymbolForm::Half;
        EXPECT_EQ(KanaModeCharacter(key, settings), std::u16string(1, key)) << static_cast<int>(key);

        settings.symbols[key] = SymbolForm::Full;
        EXPECT_EQ(KanaModeCharacter(key, settings), std::u16string(1, static_cast<char16_t>(key - 0x21 + 0xFF01)))
            << static_cast<int>(key);
    }
}

TEST(CharacterRules, JapaneseFormFallsBackToFullWidth)
{
    CharacterSettings settings;
    settings.symbols[u'{'] = SymbolForm::Japanese;
    settings.symbols[u'~'] = SymbolForm::Japanese;
    settings.symbols[u'+'] = SymbolForm::Japanese;
    EXPECT_EQ(KanaModeCharacter(u'{', settings), u"『");
    EXPECT_EQ(KanaModeCharacter(u'~', settings), u"〜");
    EXPECT_EQ(KanaModeCharacter(u'+', settings), u"＋");
}

// T-C02-1
TEST(CharacterRules, PunctuationStyles)
{
    struct Case {
        PunctuationStyle style;
        std::u16string_view comma;
        std::u16string_view period;
    };
    const Case cases[] = {
        {PunctuationStyle::ToutenKuten, u"、", u"。"},
        {PunctuationStyle::FullCommaPeriod, u"，", u"．"},
        {PunctuationStyle::HalfCommaPeriod, u",", u"."},
        {PunctuationStyle::ToutenFullPeriod, u"、", u"．"},
    };
    for (const Case& c : cases) {
        CharacterSettings settings;
        settings.punctuation = c.style;
        EXPECT_EQ(KanaModeCharacter(u',', settings), c.comma);
        EXPECT_EQ(KanaModeCharacter(u'.', settings), c.period);
    }
}

// T-C03-1
TEST(CharacterRules, LettersDigitsAndSpaceWidthsAreIndependent)
{
    CharacterSettings settings;
    settings.digits = Width::Full;
    EXPECT_EQ(KanaModeCharacter(u'1', settings), u"１");
    EXPECT_EQ(AlphanumericModeCharacter(u'G', settings), u"G");
    EXPECT_EQ(AlphanumericModeCharacter(u'1', settings), u"１");
    EXPECT_EQ(SpaceCharacter(settings), u" ");

    settings = {};
    settings.letters = Width::Full;
    EXPECT_EQ(AlphanumericModeCharacter(u'G', settings), u"Ｇ");
    EXPECT_EQ(KanaModeCharacter(u'1', settings), u"1");

    settings = {};
    settings.space = Width::Full;
    EXPECT_EQ(SpaceCharacter(settings), u"\u3000");
    EXPECT_EQ(KanaModeCharacter(u'1', settings), u"1");
}

} // namespace
} // namespace astelio
