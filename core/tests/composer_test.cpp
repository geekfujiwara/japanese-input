#include "astelio/composer.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace astelio {
namespace {

Composer MakeComposer(CharacterSettings settings = {})
{
    return Composer(RomajiTable::Default(), settings);
}

void Type(Composer& composer, std::u16string_view keys)
{
    for (char16_t key : keys) {
        composer.InsertKey(key);
    }
}

std::u16string Typed(std::u16string_view keys, CharacterSettings settings = {})
{
    Composer composer = MakeComposer(settings);
    Type(composer, keys);
    return composer.Text();
}

// T-B01-1
TEST(Composer, ConvertsEveryDefaultRomajiRule)
{
    for (const auto& [input, rule] : RomajiTable::Default().rules()) {
        if (!rule.pending.empty()) {
            continue;
        }
        Composer composer = MakeComposer();
        Type(composer, input);
        EXPECT_EQ(composer.Commit(), rule.output) << "input length " << input.size();
    }
}

// T-B01-5: z + key gives arrows and symbols, as in Google Japanese Input.
TEST(Composer, ZKeySymbols)
{
    EXPECT_EQ(Typed(u"zh"), u"←");
    EXPECT_EQ(Typed(u"zj"), u"↓");
    EXPECT_EQ(Typed(u"zk"), u"↑");
    EXPECT_EQ(Typed(u"zl"), u"→");
    EXPECT_EQ(Typed(u"z-"), u"〜");
    EXPECT_EQ(Typed(u"z."), u"…");
    EXPECT_EQ(Typed(u"z,"), u"‥");
    EXPECT_EQ(Typed(u"z/"), u"・");
    EXPECT_EQ(Typed(u"z["), u"『");
    EXPECT_EQ(Typed(u"z]"), u"』");
    EXPECT_EQ(Typed(u"zazl"), u"ざ→");
    EXPECT_EQ(Typed(u"zzl"), u"っ→") << "a doubled z is still a small tsu";
}

// T-B01-2
TEST(Composer, ConvertsDigraphsNAndSmallTsu)
{
    EXPECT_EQ(Typed(u"kya"), u"きゃ");
    EXPECT_EQ(Typed(u"nn"), u"ん");
    EXPECT_EQ(Typed(u"n'a"), u"んあ");
    EXPECT_EQ(Typed(u"xtu"), u"っ");
    EXPECT_EQ(Typed(u"ltu"), u"っ");
    EXPECT_EQ(Typed(u"konnnichiha"), u"こんにちは");
    EXPECT_EQ(Typed(u"kanji"), u"かんじ");
}

// T-B01-3
TEST(Composer, DoubledConsonantBecomesSmallTsu)
{
    EXPECT_EQ(Typed(u"kk"), u"っk");
    EXPECT_EQ(Typed(u"tt"), u"っt");
    EXPECT_EQ(Typed(u"kitte"), u"きって");
}

// T-B01-4
TEST(Composer, EditsAtTheCursor)
{
    Composer composer = MakeComposer();
    Type(composer, u"aiu");
    composer.MoveLeft();
    EXPECT_EQ(composer.Cursor(), 2u);

    composer.Backspace();
    EXPECT_EQ(composer.Text(), u"あう");
    EXPECT_EQ(composer.Cursor(), 1u);

    composer.Delete();
    EXPECT_EQ(composer.Text(), u"あ");

    composer.MoveRight();
    EXPECT_EQ(composer.Cursor(), 1u);
}

TEST(Composer, InsertsInTheMiddle)
{
    Composer composer = MakeComposer();
    Type(composer, u"au");
    composer.MoveLeft();
    composer.InsertKey(u'i');
    EXPECT_EQ(composer.Text(), u"あいう");
    EXPECT_EQ(composer.Cursor(), 2u);
}

TEST(Composer, BackspaceRemovesPendingRomajiFirst)
{
    Composer composer = MakeComposer();
    Type(composer, u"aky");
    composer.Backspace();
    EXPECT_EQ(composer.Text(), u"あk");
    composer.Backspace();
    EXPECT_EQ(composer.Text(), u"あ");
}

TEST(Composer, MovingTheCursorResolvesPendingRomaji)
{
    Composer composer = MakeComposer();
    Type(composer, u"an");
    composer.MoveLeft();
    EXPECT_EQ(composer.Text(), u"あん");
    EXPECT_EQ(composer.Cursor(), 1u);
}

TEST(Composer, CommitResolvesPendingAndResets)
{
    Composer composer = MakeComposer();
    Type(composer, u"hon");
    EXPECT_EQ(composer.Commit(), u"ほん");
    EXPECT_TRUE(composer.Empty());

    Type(composer, u"k");
    EXPECT_EQ(composer.Commit(), u"k");
}

// T-R01-1
TEST(Composer, TypesHiraganaInKanaMode)
{
    Composer composer = MakeComposer();
    composer.InsertKey(u'a');
    EXPECT_EQ(composer.Text(), u"あ");
    EXPECT_EQ(composer.Cursor(), 1u);
}

// T-R01-2
TEST(Composer, ShiftLetterStartsTemporaryAlphanumeric)
{
    Composer composer = MakeComposer();
    Type(composer, u"GitHub");
    EXPECT_EQ(composer.Text(), u"GitHub");
    EXPECT_TRUE(composer.IsTemporaryAlphanumeric());

    composer.ExitTemporaryAlphanumeric();
    Type(composer, u"wo");
    EXPECT_EQ(composer.Text(), u"GitHubを");

    composer.Commit();
    EXPECT_FALSE(composer.IsTemporaryAlphanumeric());
}

TEST(Composer, TemporaryAlphanumericKeepsSymbolsAsTyped)
{
    EXPECT_EQ(Typed(u"Node.js"), u"Node.js");
}

TEST(Composer, PendingRomajiIsResolvedBeforeUppercase)
{
    EXPECT_EQ(Typed(u"anA"), u"あんA");
}

// T-R01-4
TEST(Composer, DigitsAreHalfWidthByDefault)
{
    EXPECT_EQ(Typed(u"123"), u"123");
    EXPECT_EQ(Typed(u"n1"), u"ん1");
}

// T-R01-5
TEST(Composer, FullWidthLettersSetting)
{
    CharacterSettings settings;
    settings.letters = Width::Full;
    EXPECT_EQ(Typed(u"A", settings), u"Ａ");
}

// T-R02-1
TEST(Composer, ShiftSymbolsAreHalfWidth)
{
    CharacterSettings settings;
    settings.auto_close_brackets = false; // this test is about the characters; R-11 is tested below
    EXPECT_EQ(Typed(u"+", settings), u"+");
    EXPECT_EQ(Typed(u"(", settings), u"(");
    EXPECT_EQ(Typed(u")", settings), u")");
    EXPECT_EQ(Typed(u"a+b", settings), u"あ+b");
}

// T-R02-2
TEST(Composer, ExclamationAndQuestionAreFullWidthByDefault)
{
    EXPECT_EQ(Typed(u"!"), u"！");
    EXPECT_EQ(Typed(u"?"), u"？");
}

// T-R02-3, T-R05-1
TEST(Composer, EveryUsSymbolKeyFollowsTheDefaultRules)
{
    struct Case {
        char16_t key;
        std::u16string_view expected;
    };
    const Case cases[] = {
        {u'`', u"`"}, {u'~', u"~"}, {u'!', u"！"}, {u'@', u"@"}, {u'#', u"#"}, {u'$', u"$"},
        {u'%', u"%"}, {u'^', u"^"}, {u'&', u"&"}, {u'*', u"*"}, {u'(', u"("}, {u')', u")"},
        {u'-', u"ー"}, {u'_', u"_"}, {u'=', u"="}, {u'+', u"+"}, {u'[', u"「"}, {u'{', u"{"},
        {u']', u"」"}, {u'}', u"}"}, {u'\\', u"\\"}, {u'|', u"|"}, {u';', u";"}, {u':', u":"},
        {u'\'', u"'"}, {u'"', u"\""}, {u',', u"、"}, {u'<', u"<"}, {u'.', u"。"}, {u'>', u">"},
        {u'/', u"・"}, {u'?', u"？"},
    };
    CharacterSettings single; // one character per key; R-11 would add the closing brackets
    single.auto_close_brackets = false;
    for (const Case& c : cases) {
        EXPECT_EQ(Typed(std::u16string(1, c.key), single), c.expected) << "key " << static_cast<int>(c.key);
    }
}

// T-R10-1: '.' after a number that starts the text is ". " (a list number); a digit after it makes a decimal
// point instead. It can be turned off.
TEST(Composer, PeriodAfterAListNumber)
{
    EXPECT_EQ(Typed(u"1."), u"1. ");
    EXPECT_EQ(Typed(u"12.ringo"), u"12. りんご");
    EXPECT_EQ(Typed(u"3.14"), u"3.14");
    EXPECT_EQ(Typed(u"a1."), u"あ1。") << "only when the text starts with the number";
    EXPECT_EQ(Typed(u"."), u"。");
    CharacterSettings off;
    off.list_number_period = false;
    EXPECT_EQ(Typed(u"1.", off), u"1。");
}

// T-R11-1: an opening bracket key writes the pair with the cursor between; the closing key moves over the bracket,
// and Backspace on an empty pair removes both. It can be turned off.
TEST(Composer, BracketsAreClosedAutomatically)
{
    Composer composer = MakeComposer();
    composer.InsertKey(u'[');
    EXPECT_EQ(composer.Text(), u"「」");
    EXPECT_EQ(composer.Cursor(), 1u);
    Type(composer, u"kagi");
    EXPECT_EQ(composer.Text(), u"「かぎ」");
    EXPECT_EQ(composer.Cursor(), 3u);
    composer.InsertKey(u']');
    EXPECT_EQ(composer.Text(), u"「かぎ」") << "the closing key moves over the bracket";
    EXPECT_EQ(composer.Cursor(), 4u);

    EXPECT_EQ(Typed(u"("), u"()");
    EXPECT_EQ(Typed(u"{"), u"{}");
    EXPECT_EQ(Typed(u"()"), u"()");
    EXPECT_EQ(Typed(u"Ab["), u"Ab[]") << "in temporary alphanumeric mode too";
    EXPECT_EQ(Typed(u"z["), u"『") << "z-key symbols are written as they are";

    Composer pair = MakeComposer();
    pair.InsertKey(u'(');
    pair.Backspace();
    EXPECT_TRUE(pair.Empty()) << "an empty pair goes as a whole";

    CharacterSettings off;
    off.auto_close_brackets = false;
    EXPECT_EQ(Typed(u"[", off), u"「");
}

TEST(Composer, LongVowelMark)
{
    EXPECT_EQ(Typed(u"ra-menn"), u"らーめん");
}

} // namespace
} // namespace astelio
