#include "astelio/input_session.h"

#include "astelio/dictionary_builder.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <vector>

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

KeyEvent Arrow(KeyKind kind, bool shift = false)
{
    return {kind, 0, shift};
}

class ConversionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        constexpr std::uint16_t kIds = 5; // 0 edge, 1 noun, 2 particle, 3 verb, 4 auxiliary
        ConnectionMatrix matrix;
        matrix.size = kIds;
        matrix.costs.assign(kIds * kIds, 100);
        matrix.word_types = {WordType::Edge, WordType::Content, WordType::Suffix, WordType::Content,
                             WordType::Suffix};
        matrix.unknown_id = 1;
        matrix.unknown_cost = 5000;
        matrix.costs[0 * kIds + 1] = 0;
        matrix.costs[1 * kIds + 2] = 0;
        matrix.costs[2 * kIds + 1] = 50;
        matrix.costs[1 * kIds + 4] = 0;
        matrix.costs[4 * kIds + 0] = 0;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"わたし", u"私", 1, 1, 0, 300});
        builder.Add({u"わたし", u"渡し", 3, 3, 0, 900});
        builder.Add({u"は", u"は", 2, 2, 0, 50});
        builder.Add({u"にほんご", u"日本語", 1, 1, 0, 400});
        builder.Add({u"です", u"です", 4, 4, 0, 100});
        bytes_ = builder.Build();
        dictionary_ = SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
        converter_.emplace(*dictionary_);
        session_.SetConverter(&*converter_);
    }

    std::vector<std::u16string> Readings() const
    {
        std::vector<std::u16string> readings;
        for (const ConvertedSegment& segment : session_.Segments()) {
            readings.push_back(segment.reading);
        }
        return readings;
    }

    std::vector<std::byte> bytes_;
    std::optional<SystemDictionary> dictionary_;
    std::optional<Converter> converter_;
    InputSession session_ = MakeSession();
};

// T-B02-1, T-B06-1: Space converts, Enter commits.
TEST_F(ConversionTest, SpaceConvertsAndEnterCommits)
{
    Type(session_, u"watasihanihongodesu");
    const SessionOutput converted = session_.Handle(Key(KeyKind::Space));
    EXPECT_TRUE(converted.composition_changed);
    ASSERT_TRUE(session_.Converting());
    EXPECT_EQ(session_.CompositionText(), u"私は日本語です");
    EXPECT_EQ(Readings(), (std::vector<std::u16string>{u"わたしは", u"にほんごです"}));
    EXPECT_EQ(session_.FocusedSegment(), 0u);

    const SessionOutput committed = session_.Handle(Key(KeyKind::Enter));
    EXPECT_EQ(committed.commit, u"私は日本語です");
    EXPECT_FALSE(session_.Composing());
    EXPECT_FALSE(session_.Converting());
}

// B-02, B-03: Space / Down / Up choose candidates of the focused segment.
TEST_F(ConversionTest, SpaceAndArrowsChooseCandidates)
{
    Type(session_, u"watasihanihongodesu");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Key(KeyKind::Space));
    EXPECT_EQ(session_.CompositionText(), u"渡しは日本語です");
    EXPECT_EQ(session_.SelectedCandidate(0), 1u);
    session_.Handle(Arrow(KeyKind::Up));
    EXPECT_EQ(session_.CompositionText(), u"私は日本語です");
    session_.Handle(Arrow(KeyKind::Up));
    EXPECT_EQ(session_.SelectedCandidate(0), session_.Segments()[0].candidates.size() - 1) << "wraps around";
}

// T-B02-3: Left / Right move the focused segment.
TEST_F(ConversionTest, ArrowsMoveTheFocus)
{
    Type(session_, u"watasihanihongodesu");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Arrow(KeyKind::Right));
    EXPECT_EQ(session_.FocusedSegment(), 1u);
    session_.Handle(Arrow(KeyKind::Right));
    EXPECT_EQ(session_.FocusedSegment(), 1u);
    session_.Handle(Key(KeyKind::Space));
    EXPECT_EQ(session_.CompositionText(), u"私はにほんごです");
    session_.Handle(Arrow(KeyKind::Left));
    EXPECT_EQ(session_.FocusedSegment(), 0u);
    session_.Handle(Arrow(KeyKind::Left));
    EXPECT_EQ(session_.FocusedSegment(), 0u);
}

// T-B02-2: Shift+Left / Shift+Right resize the focused segment and convert the rest again.
TEST_F(ConversionTest, ShiftArrowsResizeTheFocusedSegment)
{
    Type(session_, u"watasihanihongodesu");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Arrow(KeyKind::Left, true));
    EXPECT_EQ(Readings(), (std::vector<std::u16string>{u"わたし", u"は", u"にほんごです"}));
    EXPECT_EQ(session_.CompositionText(), u"私は日本語です");
    session_.Handle(Arrow(KeyKind::Right, true));
    EXPECT_EQ(Readings().front(), u"わたしは");

    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Arrow(KeyKind::Right));
    session_.Handle(Arrow(KeyKind::Left, true));
    EXPECT_EQ(Readings().front(), u"わたしは");
    EXPECT_EQ(session_.CompositionText().substr(0, 3), u"渡しは") << "earlier choices are kept";
}

// T-B06-1: Esc returns to the kana, a second Esc clears.
TEST_F(ConversionTest, EscapeReturnsToKanaThenClears)
{
    Type(session_, u"watasiha");
    session_.Handle(Key(KeyKind::Space));
    ASSERT_TRUE(session_.Converting());
    session_.Handle(Key(KeyKind::Escape));
    EXPECT_FALSE(session_.Converting());
    EXPECT_EQ(session_.CompositionText(), u"わたしは");
    EXPECT_EQ(session_.CompositionCursor(), 4u);
    session_.Handle(Key(KeyKind::Escape));
    EXPECT_FALSE(session_.Composing());
}

TEST_F(ConversionTest, TypingWhileConvertingCommitsAndStartsANewComposition)
{
    Type(session_, u"watasiha");
    session_.Handle(Key(KeyKind::Space));
    const SessionOutput output = session_.Handle(Char(u'a'));
    EXPECT_EQ(output.commit, u"私は");
    EXPECT_EQ(session_.CompositionText(), u"あ");
    EXPECT_FALSE(session_.Converting());
}

// T-R03-3: switching to English while converting commits the conversion.
TEST_F(ConversionTest, LeavingJapaneseModeCommitsTheConversion)
{
    Type(session_, u"watasiha");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_EQ(session_.SetJapaneseMode(false).commit, u"私は");
    EXPECT_FALSE(session_.Composing());
}

// T-B03-1: the second Space opens the candidate list; moving the focus closes it.
TEST_F(ConversionTest, SecondSpaceOpensTheCandidateList)
{
    Type(session_, u"watasihanihongodesu");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_FALSE(session_.CandidateListVisible());
    session_.Handle(Key(KeyKind::Space));
    EXPECT_TRUE(session_.CandidateListVisible());
    session_.Handle(Arrow(KeyKind::Right));
    EXPECT_FALSE(session_.CandidateListVisible());
    session_.Handle(Arrow(KeyKind::Down));
    EXPECT_TRUE(session_.CandidateListVisible());
    session_.Handle(Key(KeyKind::Enter));
    EXPECT_FALSE(session_.CandidateListVisible());
}

// T-B03-2: number keys pick from the page, PageDown / PageUp move by a page.
TEST_F(ConversionTest, CandidateListKeys)
{
    Type(session_, u"watasiha");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_FALSE(session_.Handle(Char(u'3')).commit.empty()) << "digits commit while the list is closed";
    session_.Handle(Key(KeyKind::Escape));

    Type(session_, u"watasiha");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Key(KeyKind::Space));
    const SessionOutput picked = session_.Handle(Char(u'3'));
    EXPECT_TRUE(picked.commit.empty());
    EXPECT_EQ(session_.SelectedCandidate(0), 2u);
    EXPECT_EQ(session_.CompositionText(), u"わたしは");
    EXPECT_FALSE(session_.CandidateListVisible());
    EXPECT_TRUE(session_.Converting());

    session_.Handle(Key(KeyKind::PageUp));
    EXPECT_EQ(session_.SelectedCandidate(0), 0u);
    EXPECT_TRUE(session_.CandidateListVisible());
    session_.Handle(Key(KeyKind::PageDown));
    EXPECT_EQ(session_.SelectedCandidate(0), session_.Segments()[0].candidates.size() - 1);
    session_.Handle(Char(u'9'));
    EXPECT_EQ(session_.SelectedCandidate(0), session_.Segments()[0].candidates.size() - 1)
        << "a number past the end keeps the choice";
}

TEST_F(ConversionTest, ArrowKeysWhileComposingDoNotReachTheApp)
{
    EXPECT_FALSE(session_.WillHandle(Arrow(KeyKind::Down)));
    Type(session_, u"a");
    EXPECT_TRUE(session_.WillHandle(Arrow(KeyKind::Down)));
    EXPECT_FALSE(session_.Handle(Arrow(KeyKind::Down)).composition_changed);
}

} // namespace
} // namespace astelio
