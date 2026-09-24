#include "astelio/input_session.h"

#include <gtest/gtest.h>

#include <string_view>

namespace astelio {
namespace {

KeyEvent Char(char16_t c)
{
    return {KeyKind::Character, c};
}

KeyEvent Key(KeyKind kind)
{
    return {kind, 0};
}

InputSession MakeSession(CharacterSettings settings = {})
{
    return InputSession(RomajiTable::Default(), settings);
}

void Type(InputSession& session, std::u16string_view keys)
{
    for (char16_t c : keys) {
        session.Handle(Char(c));
    }
}

// T-R01-1
TEST(InputSession, CharactersStartAComposition)
{
    InputSession session = MakeSession();
    EXPECT_TRUE(session.WillHandle(Char(u'a')));
    const SessionOutput output = session.Handle(Char(u'a'));
    EXPECT_TRUE(output.composition_changed);
    EXPECT_TRUE(output.commit.empty());
    EXPECT_EQ(session.CompositionText(), u"あ");
}

// T-B06-1 (before conversion)
TEST(InputSession, EnterCommitsAndEscapeCancels)
{
    InputSession session = MakeSession();
    Type(session, u"ka");
    EXPECT_EQ(session.Handle(Key(KeyKind::Enter)).commit, u"か");
    EXPECT_FALSE(session.Composing());

    Type(session, u"ki");
    const SessionOutput cancelled = session.Handle(Key(KeyKind::Escape));
    EXPECT_TRUE(cancelled.commit.empty());
    EXPECT_TRUE(cancelled.composition_changed);
    EXPECT_FALSE(session.Composing());
}

TEST(InputSession, EditingKeysPassThroughWithoutAComposition)
{
    InputSession session = MakeSession();
    for (KeyKind kind : {KeyKind::Enter, KeyKind::Escape, KeyKind::Backspace, KeyKind::Delete, KeyKind::Left,
                         KeyKind::Right}) {
        EXPECT_FALSE(session.WillHandle(Key(kind)));
    }
    Type(session, u"a");
    for (KeyKind kind : {KeyKind::Enter, KeyKind::Escape, KeyKind::Backspace, KeyKind::Delete, KeyKind::Left,
                         KeyKind::Right}) {
        EXPECT_TRUE(session.WillHandle(Key(kind)));
    }
}

TEST(InputSession, BackspaceEditsTheComposition)
{
    InputSession session = MakeSession();
    Type(session, u"aiu");
    session.Handle(Key(KeyKind::Left));
    session.Handle(Key(KeyKind::Backspace));
    EXPECT_EQ(session.CompositionText(), u"あう");
    EXPECT_EQ(session.CompositionCursor(), 1u);
}

// C-03: Space outside a composition types the configured width.
TEST(InputSession, SpaceOutsideACompositionTypesASpace)
{
    CharacterSettings settings;
    settings.space = Width::Full;
    InputSession session = MakeSession(settings);
    EXPECT_TRUE(session.WillHandle(Key(KeyKind::Space)));
    EXPECT_EQ(session.Handle(Key(KeyKind::Space)).commit, u"\u3000");
    EXPECT_FALSE(session.Composing());
}

TEST(InputSession, SpaceInsideACompositionKeepsIt)
{
    InputSession session = MakeSession();
    Type(session, u"a");
    const SessionOutput output = session.Handle(Key(KeyKind::Space));
    EXPECT_TRUE(output.commit.empty());
    EXPECT_EQ(session.CompositionText(), u"あ");
}

// T-R03-3: switching to English commits the uncommitted text.
TEST(InputSession, LeavingJapaneseModeCommitsAndPassesKeys)
{
    InputSession session = MakeSession();
    Type(session, u"a");
    EXPECT_EQ(session.SetJapaneseMode(false).commit, u"あ");
    EXPECT_FALSE(session.Composing());
    EXPECT_FALSE(session.WillHandle(Char(u'a')));
    EXPECT_FALSE(session.WillHandle(Key(KeyKind::Space)));

    EXPECT_TRUE(session.SetJapaneseMode(true).commit.empty());
    EXPECT_TRUE(session.WillHandle(Char(u'a')));
}

TEST(InputSession, AbandonedCompositionIsCleared)
{
    InputSession session = MakeSession();
    Type(session, u"a");
    session.AbandonComposition();
    EXPECT_FALSE(session.Composing());
}

} // namespace
} // namespace astelio
