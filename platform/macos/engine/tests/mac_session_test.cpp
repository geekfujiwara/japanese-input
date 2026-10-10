#include "astelio/mac/mac_session.h"

#include "test_dictionary.h"

#include <gtest/gtest.h>

#include <string_view>

namespace astelio::mac {
namespace {

// Key codes of the US layout.
constexpr std::uint16_t kSpace = 0x31;
constexpr std::uint16_t kReturn = 0x24;
constexpr std::uint16_t kBackspace = 0x33;
constexpr std::uint16_t kDigit1 = 0x12;
constexpr std::uint16_t kDigit2 = 0x13;

std::uint16_t LetterCode(char c)
{
    switch (c) {
    case 'a': return 0x00;
    case 'w': return 0x0D;
    case 't': return 0x11;
    case 's': return 0x01;
    case 'h': return 0x04;
    case 'i': return 0x22;
    case 'n': return 0x2D;
    case 'u': return 0x20;
    case 'o': return 0x1F;
    case 'g': return 0x05;
    case 'e': return 0x0E;
    case 'm': return 0x2E;
    case 'j': return 0x26;
    default: ADD_FAILURE() << "no key code for " << c; return 0x7F;
    }
}

class MacSessionTest : public ::testing::Test {
protected:
    MacSessionTest() : engine_(files_.WriteDictionary(), files_.data()), session_(engine_) {}

    std::u16string Type(std::string_view keys)
    {
        std::u16string committed;
        for (char c : keys) {
            const KeyResult result = session_.KeyDown(LetterCode(c), {});
            EXPECT_TRUE(result.handled);
            committed += result.output.commit;
        }
        return committed;
    }

    KeyResult Press(std::uint16_t code, Modifiers modifiers = {}) { return session_.KeyDown(code, modifiers); }

    testing::TemporaryFiles files_;
    Engine engine_;
    MacSession session_;
};

// T-B02-1 on macOS: type, convert with Space, commit with Return.
TEST_F(MacSessionTest, TypesConvertsAndCommits)
{
    Type("watashiha");
    Composition typing = session_.CurrentComposition();
    EXPECT_EQ(typing.text, u"わたしは");
    EXPECT_EQ(typing.cursor, 4u);
    ASSERT_EQ(typing.spans.size(), 1u);
    EXPECT_EQ(typing.spans[0].style, SpanStyle::Input);

    EXPECT_TRUE(Press(kSpace).handled);
    Composition converted = session_.CurrentComposition();
    EXPECT_EQ(converted.text, u"私は");
    ASSERT_FALSE(converted.spans.empty());
    EXPECT_EQ(converted.spans[0].style, SpanStyle::Focused);

    const KeyResult commit = Press(kReturn);
    EXPECT_TRUE(commit.handled);
    EXPECT_EQ(commit.output.commit, u"私は");
    EXPECT_FALSE(session_.Composing());
}

TEST_F(MacSessionTest, SecondSpaceOpensTheCandidates)
{
    Type("watashi");
    Press(kSpace);
    EXPECT_EQ(session_.Candidates().kind, CandidateKind::Hidden);
    Press(kSpace);
    const CandidateView view = session_.Candidates();
    ASSERT_EQ(view.kind, CandidateKind::Candidates);
    ASSERT_GE(view.items.size(), 2u);
    EXPECT_EQ(view.items[0], u"私");
    EXPECT_EQ(view.items[1], u"渡し");
    EXPECT_EQ(view.selected, 1u);
    EXPECT_EQ(session_.CurrentComposition().text, u"渡し");

    // The digit picks from the column shown and closes it.
    Press(kDigit1);
    EXPECT_EQ(session_.CurrentComposition().text, u"私");
    EXPECT_EQ(session_.Candidates().kind, CandidateKind::Hidden);
}

TEST_F(MacSessionTest, ClickingACandidatePicksIt)
{
    Type("watashi");
    Press(kSpace);
    Press(kSpace);
    session_.Pick(1);
    EXPECT_EQ(session_.CurrentComposition().text, u"渡し");
    EXPECT_EQ(Press(kReturn).output.commit, u"渡し");
}

TEST_F(MacSessionTest, PredictionsWhileTyping)
{
    Type("nihon");
    const CandidateView view = session_.Candidates();
    ASSERT_EQ(view.kind, CandidateKind::Predictions);
    ASSERT_FALSE(view.items.empty());
    EXPECT_EQ(view.items[0], u"日本語");
    EXPECT_EQ(view.selected, kNoEmojiSelection);
}

// D-04: the choice is saved and comes first next time, in another text field too.
TEST_F(MacSessionTest, LearnsAndSavesTheChoice)
{
    Type("watashi");
    Press(kSpace);
    Press(kSpace);
    Press(kReturn);
    EXPECT_TRUE(std::filesystem::exists(engine_.LearningPath()));

    MacSession other(engine_);
    for (char c : std::string_view("watashi")) {
        other.KeyDown(LetterCode(c), {});
    }
    other.KeyDown(kSpace, {});
    EXPECT_EQ(other.CurrentComposition().text, u"渡し");
}

TEST_F(MacSessionTest, NothingIsRecordedInSecureInput)
{
    session_.SetRecordingAllowed(false);
    Type("watashi");
    Press(kSpace);
    Press(kSpace);
    Press(kReturn);
    EXPECT_TRUE(engine_.learning().Empty());
}

TEST_F(MacSessionTest, NothingIsUsedWithTheHistoryOff)
{
    Settings settings;
    settings.learning_enabled = false;
    session_.ApplySettings(settings);
    Type("watashi");
    Press(kSpace);
    Press(kSpace);
    Press(kReturn);
    EXPECT_TRUE(engine_.learning().Empty());
}

TEST_F(MacSessionTest, BackspaceAndEnglishMode)
{
    Type("ai");
    Press(kBackspace);
    EXPECT_EQ(session_.CurrentComposition().text, u"あ");
    const SessionOutput output = session_.SetJapaneseMode(false);
    EXPECT_EQ(output.commit, u"あ");
    EXPECT_FALSE(Press(LetterCode('a')).handled);
    EXPECT_FALSE(Press(kBackspace).handled);
}

TEST_F(MacSessionTest, ShortcutsGoToTheApp)
{
    EXPECT_FALSE(Press(LetterCode('a'), {.command = true}).handled);
    Type("a");
    EXPECT_FALSE(Press(LetterCode('a'), {.command = true}).handled);
}

// R-03 / C-06: a single press of left Command is English, right Command is Japanese.
TEST_F(MacSessionTest, CommandTapsSwitchTheMode)
{
    EXPECT_FALSE(session_.ModifierChanged(kLeftCommandKeyCode, true, 0));
    EXPECT_EQ(session_.ModifierChanged(kLeftCommandKeyCode, false, 100), std::optional<bool>(false));
    EXPECT_FALSE(session_.ModifierChanged(kRightCommandKeyCode, true, 200));
    EXPECT_EQ(session_.ModifierChanged(kRightCommandKeyCode, false, 300), std::optional<bool>(true));
}

TEST_F(MacSessionTest, CommandShortcutIsNotATap)
{
    session_.ModifierChanged(kLeftCommandKeyCode, true, 0);
    Press(LetterCode('a'), {.command = true});
    EXPECT_FALSE(session_.ModifierChanged(kLeftCommandKeyCode, false, 100));

    session_.ModifierChanged(kLeftCommandKeyCode, true, 200);
    session_.MarkChordUsed(); // a click
    EXPECT_FALSE(session_.ModifierChanged(kLeftCommandKeyCode, false, 300));

    session_.ModifierChanged(kLeftCommandKeyCode, true, 400);
    session_.ModifierChanged(0x38, true, 450); // Shift
    EXPECT_FALSE(session_.ModifierChanged(kLeftCommandKeyCode, false, 500));
}

TEST_F(MacSessionTest, LongPressIsNotATap)
{
    session_.ModifierChanged(kRightCommandKeyCode, true, 0);
    EXPECT_FALSE(session_.ModifierChanged(kRightCommandKeyCode, false, 5000));
}

TEST_F(MacSessionTest, OptionCanBeTheTapKey)
{
    Settings settings;
    settings.tap_modifier = TapModifier::Option;
    session_.ApplySettings(settings);
    session_.ModifierChanged(kRightCommandKeyCode, true, 0);
    EXPECT_FALSE(session_.ModifierChanged(kRightCommandKeyCode, false, 100));
    session_.ModifierChanged(kLeftOptionKeyCode, true, 200);
    EXPECT_EQ(session_.ModifierChanged(kLeftOptionKeyCode, false, 300), std::optional<bool>(false));
}

TEST(MacSessionWithoutDictionary, TypesKana)
{
    testing::TemporaryFiles files;
    Engine engine(files.directory() / "missing.dic", files.data());
    MacSession session(engine);
    session.KeyDown(0x00, {});
    EXPECT_EQ(session.CurrentComposition().text, u"あ");
    EXPECT_EQ(session.KeyDown(kReturn, {}).output.commit, u"あ");
}

} // namespace
} // namespace astelio::mac
