#include "astelio/learning.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace astelio {
namespace {

using Kind = LearningHistory::Kind;

TEST(LearningHistory, MostRecentChoiceComesFirst)
{
    LearningHistory history;
    EXPECT_TRUE(history.Record(Kind::Conversion, u"わたし", u"渡し"));
    EXPECT_TRUE(history.Record(Kind::Conversion, u"わたし", u"私"));
    EXPECT_EQ(history.Conversions(u"わたし"), (std::vector<std::u16string>{u"私", u"渡し"}));
    EXPECT_TRUE(history.Record(Kind::Conversion, u"わたし", u"渡し"));
    EXPECT_EQ(history.Conversions(u"わたし"), (std::vector<std::u16string>{u"渡し", u"私"}));
    EXPECT_EQ(history.Entries().front().count, 2u);
    EXPECT_TRUE(history.Conversions(u"わた").empty());
    EXPECT_FALSE(history.Contains(Kind::Prediction, u"わたし", u"渡し")) << "kinds are separate";
}

TEST(LearningHistory, RejectsEmptyLongAndControlText)
{
    LearningHistory history;
    EXPECT_FALSE(history.Record(Kind::Conversion, u"", u"私"));
    EXPECT_FALSE(history.Record(Kind::Conversion, u"わたし", u""));
    EXPECT_FALSE(history.Record(Kind::Conversion, std::u16string(LearningHistory::kMaxTextLength + 1, u'あ'), u"x"));
    EXPECT_FALSE(history.Record(Kind::Conversion, u"a\tb", u"x"));
    EXPECT_FALSE(history.Record(Kind::Conversion, u"ab", u"x\ny"));
    EXPECT_TRUE(history.Empty());
}

// T-D04-3
TEST(LearningHistory, OldestEntriesGoPastTheLimit)
{
    LearningHistory history;
    for (std::size_t i = 0; i < LearningHistory::kMaxEntries + 10; ++i) {
        history.Record(Kind::Conversion, u"よみ" + std::u16string(1, static_cast<char16_t>(u'一' + i)), u"語");
    }
    EXPECT_EQ(history.Entries().size(), LearningHistory::kMaxEntries);
    EXPECT_TRUE(history.Conversions(u"よみ一").empty()) << "the oldest is gone";
    EXPECT_FALSE(history.Conversions(std::u16string(u"よみ") +
                                     static_cast<char16_t>(u'一' + LearningHistory::kMaxEntries + 9))
                     .empty());
}

// T-D05-1, T-D05-2
TEST(LearningHistory, RemovesOneOrAll)
{
    LearningHistory history;
    history.Record(Kind::Conversion, u"わたし", u"渡し");
    history.Record(Kind::Prediction, u"あり", u"有り難い");
    history.Record(Kind::Prediction, u"ありが", u"有り難い");
    EXPECT_FALSE(history.Remove(Kind::Conversion, u"わたし", u"私"));
    EXPECT_TRUE(history.Remove(Kind::Conversion, u"わたし", u"渡し"));
    EXPECT_TRUE(history.Conversions(u"わたし").empty());
    EXPECT_TRUE(history.RemoveSurface(Kind::Prediction, u"有り難い"));
    EXPECT_TRUE(history.Empty());
    history.Record(Kind::Conversion, u"わたし", u"渡し");
    history.Clear();
    EXPECT_TRUE(history.Empty());
}

TEST(LearningHistory, PredictsFromThePrefix)
{
    LearningHistory history;
    history.Record(Kind::Prediction, u"あり", u"有り難い");
    history.Record(Kind::Conversion, u"ありがとう", u"有難う");
    history.Record(Kind::Conversion, u"あり", u"蟻");
    EXPECT_EQ(history.Predictions(u"あり", 5), (std::vector<std::u16string>{u"有難う", u"有り難い"}))
        << "a conversion of the same reading is not a prediction";
    EXPECT_EQ(history.Predictions(u"ありが", 5), (std::vector<std::u16string>{u"有難う"}));
    EXPECT_EQ(history.Predictions(u"あり", 1).size(), 1u);
    EXPECT_TRUE(history.Predictions(u"", 5).empty());
}

// T-D05-3: the saved text holds only what is left.
TEST(LearningHistory, SerializesAndParses)
{
    LearningHistory history;
    history.Record(Kind::Conversion, u"わたし", u"渡し");
    history.Record(Kind::Prediction, u"あり", u"有り難い");
    history.Record(Kind::Conversion, u"ひみつ", u"秘密");
    history.Remove(Kind::Conversion, u"ひみつ", u"秘密");
    const std::string text = history.Serialize();
    EXPECT_EQ(text.find("秘密"), std::string::npos);

    const LearningHistory parsed = LearningHistory::Parse(text);
    ASSERT_EQ(parsed.Entries().size(), 2u);
    EXPECT_EQ(parsed.Entries()[0].surface, u"有り難い");
    EXPECT_EQ(parsed.Entries()[0].kind, Kind::Prediction);
    EXPECT_EQ(parsed.Entries()[1].surface, u"渡し");

    LearningHistory more = parsed;
    more.Record(Kind::Conversion, u"わたし", u"私");
    EXPECT_EQ(more.Entries().front().surface, u"私") << "the clock continues after the parsed entries";
}

TEST(LearningHistory, ParseSkipsMalformedLines)
{
    const std::string text = "# astelio learning 1\n"
                             "c\tわたし\t渡し\t1\t5\n"
                             "c\tわたし\t渡し\t3\t2\n"          // an older duplicate
                             "x\tわたし\t私\t1\t6\n"            // unknown kind
                             "c\tわたし\t\t1\t7\n"              // empty surface
                             "c\tわたし\t私\tone\t8\n"          // not a number
                             "c\tわたし\t私\t1\t9\textra\n"     // too many fields
                             "c\t\xff\xfe\t私\t1\t10\n"         // not UTF-8
                             "p\tあり\t有り難い\t2\t4\r\n";
    const LearningHistory history = LearningHistory::Parse(text);
    ASSERT_EQ(history.Entries().size(), 2u);
    EXPECT_EQ(history.Entries()[0].surface, u"渡し");
    EXPECT_EQ(history.Entries()[0].last_used, 5u);
    EXPECT_EQ(history.Entries()[1].surface, u"有り難い");
    EXPECT_TRUE(LearningHistory::Parse("").Empty());
    EXPECT_TRUE(LearningHistory::Parse("\n\n\t\t\n").Empty());
}

// T-D04-4
TEST(LearningHistory, PairsDependOnTheWordBefore)
{
    LearningHistory history;
    EXPECT_TRUE(history.Record(Kind::Pair, u"はし", u"箸", u"お"));
    EXPECT_TRUE(history.Record(Kind::Pair, u"はし", u"橋", u"川の"));
    EXPECT_EQ(history.Pairs(u"お", u"はし"), (std::vector<std::u16string>{u"箸"}));
    EXPECT_EQ(history.Pairs(u"川の", u"はし"), (std::vector<std::u16string>{u"橋"}));
    EXPECT_TRUE(history.Pairs(u"", u"はし").empty());
    EXPECT_TRUE(history.Conversions(u"はし").empty()) << "pairs are separate";
    EXPECT_TRUE(history.Predictions(u"は", 5).empty());
    EXPECT_FALSE(history.Record(Kind::Pair, u"はし", u"箸")) << "a pair needs the word before";
    EXPECT_FALSE(history.Record(Kind::Conversion, u"はし", u"箸", u"お")) << "only pairs have a word before";
    EXPECT_TRUE(history.Remove(Kind::Pair, u"はし", u"箸", u"お"));
    EXPECT_TRUE(history.Pairs(u"お", u"はし").empty());
}

// T-D04-2
TEST(LearningHistory, OneSegmentationPerReading)
{
    LearningHistory history;
    EXPECT_EQ(LearningHistory::JoinSegments({u"わたし", u"は"}), u"わたし|は");
    EXPECT_TRUE(history.Record(Kind::Segmentation, u"わたしは", u"わたし|は"));
    EXPECT_EQ(history.Segmentation(u"わたしは"), (std::vector<std::size_t>{3, 1}));
    EXPECT_TRUE(history.Record(Kind::Segmentation, u"わたしは", u"わた|しは"));
    EXPECT_EQ(history.Segmentation(u"わたしは"), (std::vector<std::size_t>{2, 2}));
    EXPECT_EQ(history.Entries().size(), 1u) << "the older split is replaced";
    EXPECT_FALSE(history.Segmentation(u"わたし"));
    EXPECT_FALSE(history.Record(Kind::Segmentation, u"わたしは", u"わたし|が")) << "must spell the reading";
    EXPECT_FALSE(history.Record(Kind::Segmentation, u"わたしは", u"わたし||は")) << "no empty segment";
}

// T-D05-2
TEST(LearningHistory, RemovesWhatWasUsedInAPeriod)
{
    std::int64_t now = 1'000'000;
    LearningHistory history;
    history.SetClock([&now] { return now; });
    history.Record(Kind::Conversion, u"ふるい", u"古い");
    now += 3600;
    history.Record(Kind::Conversion, u"あたらしい", u"新しい");
    history.Record(Kind::Pair, u"はし", u"箸", u"お");
    EXPECT_EQ(history.Entries().front().time, now);
    EXPECT_EQ(history.RemoveSince(now - 60), 2u) << "the last minute";
    ASSERT_EQ(history.Entries().size(), 1u);
    EXPECT_EQ(history.Entries().front().surface, u"古い");
    EXPECT_EQ(history.RemoveSince(0), 1u);
    EXPECT_TRUE(history.Empty());
}

TEST(LearningHistory, SerializesContextsTimesAndSegmentations)
{
    LearningHistory history;
    history.SetClock([] { return std::int64_t{1'700'000'000}; });
    history.Record(Kind::Pair, u"はし", u"箸", u"お");
    history.Record(Kind::Segmentation, u"わたしは", u"わたし|は");
    const LearningHistory parsed = LearningHistory::Parse(history.Serialize());
    ASSERT_EQ(parsed.Entries().size(), 2u);
    EXPECT_EQ(parsed.Entries()[0].kind, Kind::Segmentation);
    EXPECT_EQ(parsed.Entries()[1].context, u"お");
    EXPECT_EQ(parsed.Entries()[1].time, 1'700'000'000);
    EXPECT_EQ(parsed.Pairs(u"お", u"はし"), (std::vector<std::u16string>{u"箸"}));
    EXPECT_TRUE(LearningHistory::Parse("w\t\tはし\t箸\t1\t1\t0\n").Empty()) << "a pair without the word before";
    EXPECT_TRUE(LearningHistory::Parse("s\t\tわたしは\tわたし|が\t1\t1\t0\n").Empty());
}

} // namespace
} // namespace astelio
