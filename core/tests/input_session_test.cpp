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

// T-B05-1: F6-F10 give hiragana, katakana, half-width katakana, full-width and half-width letters.
TEST(InputSession, FunctionKeysConvertToFixedForms)
{
    InputSession session = MakeSession();
    Type(session, u"nihon");
    EXPECT_TRUE(session.WillHandle(Key(KeyKind::F7)));
    session.Handle(Key(KeyKind::F6));
    EXPECT_TRUE(session.Converting());
    EXPECT_EQ(session.CompositionText(), u"にほん");
    session.Handle(Key(KeyKind::F7));
    EXPECT_EQ(session.CompositionText(), u"ニホン");
    session.Handle(Key(KeyKind::F8));
    EXPECT_EQ(session.CompositionText(), u"ﾆﾎﾝ");
    session.Handle(Key(KeyKind::F9));
    EXPECT_EQ(session.CompositionText(), u"ｎｉｈｏｎ");
    session.Handle(Key(KeyKind::F10));
    EXPECT_EQ(session.CompositionText(), u"nihon");
    session.Handle(Key(KeyKind::F7));
    EXPECT_EQ(session.CompositionText(), u"ニホン") << "forms can be chosen again";
    EXPECT_EQ(session.Handle(Key(KeyKind::Enter)).commit, u"ニホン");
    EXPECT_FALSE(session.WillHandle(Key(KeyKind::F7))) << "nothing to convert";
}

TEST(InputSession, FunctionKeysUseTheTypedKeysOrSpellTheKana)
{
    InputSession session = MakeSession();
    Type(session, u"sinbun");
    session.Handle(Key(KeyKind::F10));
    EXPECT_EQ(session.CompositionText(), u"sinbun") << "the keys as typed";
    session.Handle(Key(KeyKind::Escape));
    session.Handle(Key(KeyKind::Escape));

    Type(session, u"sinbunn");
    session.Handle(Key(KeyKind::Backspace));
    session.Handle(Key(KeyKind::F10));
    EXPECT_EQ(session.CompositionText(), u"shinbu") << "after editing, spelled from the kana";
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
        builder.Add({u"ありがとう", u"ありがとう", 1, 1, 0, 400});
        builder.Add({u"ありがたい", u"有り難い", 1, 1, 0, 800});
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

// T-B04-1: predictions appear while typing; Tab selects them and Enter commits.
TEST_F(ConversionTest, PredictionsWhileTypingAndTabSelects)
{
    Type(session_, u"a");
    EXPECT_TRUE(session_.Predictions().empty()) << "one kana is too short";
    Type(session_, u"rig");
    ASSERT_FALSE(session_.Predictions().empty()) << "pending romaji is ignored";
    EXPECT_EQ(session_.Predictions().front(), u"ありがとう");
    EXPECT_EQ(session_.Predictions().at(1), u"有り難い");

    session_.Handle(Key(KeyKind::Tab));
    ASSERT_TRUE(session_.Converting());
    EXPECT_TRUE(session_.CandidateListVisible());
    EXPECT_TRUE(session_.Predictions().empty());
    EXPECT_EQ(session_.CompositionText(), u"ありがとう");
    session_.Handle(Key(KeyKind::Tab));
    EXPECT_EQ(session_.CompositionText(), u"有り難い");
    session_.Handle(KeyEvent{KeyKind::Tab, 0, true});
    EXPECT_EQ(session_.Handle(Key(KeyKind::Enter)).commit, u"ありがとう");
    EXPECT_FALSE(session_.Composing());
}

TEST_F(ConversionTest, EscapeFromPredictionsReturnsToTheKana)
{
    Type(session_, u"ari");
    session_.Handle(Arrow(KeyKind::Down));
    ASSERT_TRUE(session_.Converting());
    session_.Handle(Key(KeyKind::Escape));
    EXPECT_EQ(session_.CompositionText(), u"あり");
    EXPECT_FALSE(session_.Predictions().empty());
    EXPECT_FALSE(session_.Handle(Key(KeyKind::Tab)).commit.size() > 0);
}

TEST_F(ConversionTest, TabWithoutPredictionsDoesNothing)
{
    Type(session_, u"nu");
    EXPECT_TRUE(session_.Predictions().empty());
    EXPECT_TRUE(session_.WillHandle(Key(KeyKind::Tab)));
    EXPECT_FALSE(session_.Handle(Key(KeyKind::Tab)).composition_changed);
    EXPECT_FALSE(session_.Converting());
}

TEST_F(ConversionTest, ArrowKeysWhileComposingDoNotReachTheApp)
{
    EXPECT_FALSE(session_.WillHandle(Arrow(KeyKind::Down)));
    Type(session_, u"a");
    EXPECT_TRUE(session_.WillHandle(Arrow(KeyKind::Down)));
    EXPECT_FALSE(session_.Handle(Arrow(KeyKind::Down)).composition_changed);
}

KeyEvent ControlDelete()
{
    KeyEvent key{KeyKind::Delete, 0};
    key.control = true;
    return key;
}

// T-D04-1: a candidate chosen once comes first the next time.
TEST_F(ConversionTest, ChosenCandidateComesFirstNextTime)
{
    LearningHistory history;
    session_.SetLearning(&history);
    Type(session_, u"watasi");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_FALSE(session_.Handle(Key(KeyKind::Enter)).learning_changed) << "the first candidate is not learned";
    EXPECT_TRUE(history.Empty());

    Type(session_, u"watasi");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Key(KeyKind::Space));
    ASSERT_EQ(session_.CompositionText(), u"渡し");
    EXPECT_FALSE(session_.IsLearnedCandidate(0, 1));
    const SessionOutput committed = session_.Handle(Key(KeyKind::Enter));
    EXPECT_EQ(committed.commit, u"渡し");
    EXPECT_TRUE(committed.learning_changed);

    Type(session_, u"watasi");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_EQ(session_.CompositionText(), u"渡し");
    EXPECT_TRUE(session_.IsLearnedCandidate(0, 0));
    EXPECT_EQ(session_.Segments()[0].candidates.at(1), u"私");
    EXPECT_TRUE(session_.Handle(Key(KeyKind::Enter)).learning_changed) << "a learned word is refreshed";
}

// T-D05-1: Ctrl+Delete in the candidate list forgets the word; the order is as before learning.
TEST_F(ConversionTest, ControlDeleteForgetsTheSelectedCandidate)
{
    LearningHistory history;
    history.Record(LearningHistory::Kind::Conversion, u"わたし", u"渡し");
    session_.SetLearning(&history);
    Type(session_, u"watasi");
    session_.Handle(Key(KeyKind::Space));
    EXPECT_FALSE(session_.Handle(ControlDelete()).learning_changed) << "only while the list is shown";
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Arrow(KeyKind::Up));
    ASSERT_TRUE(session_.CandidateListVisible());
    ASSERT_EQ(session_.CompositionText(), u"渡し");

    const SessionOutput forgotten = session_.Handle(ControlDelete());
    EXPECT_TRUE(forgotten.learning_changed);
    EXPECT_TRUE(history.Empty());
    EXPECT_EQ(session_.Segments()[0].candidates.front(), u"私");
    EXPECT_EQ(session_.CompositionText(), u"渡し") << "the selection stays on the word";
    EXPECT_FALSE(session_.IsLearnedCandidate(0, session_.SelectedCandidate(0)));
    EXPECT_FALSE(session_.Handle(ControlDelete()).learning_changed) << "not in the history any more";
}

// T-D06-1, T-B11-1: with recording off (password fields, secret mode) nothing is learned.
TEST_F(ConversionTest, NothingIsRecordedWhileRecordingIsOff)
{
    LearningHistory history;
    session_.SetLearning(&history);
    session_.SetRecording(false);
    Type(session_, u"watasi");
    session_.Handle(Key(KeyKind::Space));
    session_.Handle(Key(KeyKind::Space));
    EXPECT_FALSE(session_.Handle(Key(KeyKind::Enter)).learning_changed);
    EXPECT_TRUE(history.Empty());
}

// T-D04-1 (predictions): a prediction chosen once is offered first.
TEST_F(ConversionTest, ChosenPredictionComesFirstAndCanBeForgotten)
{
    LearningHistory history;
    session_.SetLearning(&history);
    Type(session_, u"ari");
    session_.Handle(Key(KeyKind::Tab));
    session_.Handle(Key(KeyKind::Tab));
    ASSERT_EQ(session_.CompositionText(), u"有り難い");
    EXPECT_TRUE(session_.Handle(Key(KeyKind::Enter)).learning_changed);

    Type(session_, u"ari");
    ASSERT_FALSE(session_.Predictions().empty());
    EXPECT_EQ(session_.Predictions().front(), u"有り難い");
    session_.Handle(Key(KeyKind::Tab));
    EXPECT_TRUE(session_.IsLearnedCandidate(0, 0));
    EXPECT_TRUE(session_.Handle(ControlDelete()).learning_changed);
    EXPECT_TRUE(history.Empty());
    session_.Handle(Key(KeyKind::Escape));
    session_.Handle(Key(KeyKind::Escape));

    Type(session_, u"ari");
    EXPECT_EQ(session_.Predictions().front(), u"ありがとう");
}

} // namespace
} // namespace astelio
