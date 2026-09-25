#include "astelio/converter.h"
#include "astelio/dictionary_builder.h"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

namespace {

using astelio::ConvertedSegment;
using astelio::WordType;

// Part-of-speech ids of the test dictionary.
constexpr std::uint16_t kEdge = 0;
constexpr std::uint16_t kNoun = 1;
constexpr std::uint16_t kParticle = 2;
constexpr std::uint16_t kVerb = 3;
constexpr std::uint16_t kAuxiliary = 4;
constexpr std::uint16_t kPrefix = 5;
constexpr std::uint16_t kIds = 6;

class ConverterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        astelio::ConnectionMatrix matrix;
        matrix.size = kIds;
        matrix.bos_id = kEdge;
        matrix.eos_id = kEdge;
        matrix.costs.assign(kIds * kIds, 100);
        matrix.word_types = {WordType::Edge, WordType::Content, WordType::Suffix,
                             WordType::Content, WordType::Suffix, WordType::Prefix};
        matrix.unknown_id = kNoun;
        matrix.unknown_cost = 5000;
        const auto set = [&matrix](std::uint16_t right, std::uint16_t left, std::int16_t cost) {
            matrix.costs[right * kIds + left] = cost;
        };
        set(kEdge, kNoun, 0);
        set(kNoun, kParticle, 0);
        set(kParticle, kNoun, 50);
        set(kParticle, kEdge, 0);
        set(kNoun, kAuxiliary, 0);
        set(kAuxiliary, kEdge, 0);
        set(kPrefix, kNoun, 0);

        astelio::DictionaryBuilder builder(std::move(matrix));
        const auto add = [&builder](std::u16string reading, std::u16string surface, std::uint16_t id,
                                    std::int16_t cost) {
            ASSERT_TRUE(builder.Add({std::move(reading), std::move(surface), id, id, 0, cost}));
        };
        add(u"わたし", u"私", kNoun, 300);
        add(u"わたし", u"渡し", kVerb, 900);
        add(u"わた", u"綿", kNoun, 600);
        add(u"し", u"し", kVerb, 400);
        add(u"は", u"は", kParticle, 50);
        add(u"は", u"歯", kNoun, 900);
        add(u"にほん", u"日本", kNoun, 400);
        add(u"ご", u"語", kNoun, 500);
        add(u"にほんご", u"日本語", kNoun, 400);
        add(u"です", u"です", kAuxiliary, 100);
        add(u"お", u"お", kPrefix, 200);
        add(u"ちゃ", u"茶", kNoun, 300);
        add(u"きょう", u"今日", kNoun, 300);
        bytes_ = builder.Build();
        dictionary_ = astelio::SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
    }

    std::vector<ConvertedSegment> Convert(std::u16string_view reading, std::vector<std::size_t> fixed = {})
    {
        return astelio::Converter(*dictionary_).Convert(reading, fixed);
    }

    static std::u16string Best(const std::vector<ConvertedSegment>& segments)
    {
        std::u16string text;
        for (const ConvertedSegment& segment : segments) {
            text += segment.candidates.at(0);
        }
        return text;
    }

    static std::vector<std::u16string> Readings(const std::vector<ConvertedSegment>& segments)
    {
        std::vector<std::u16string> readings;
        for (const ConvertedSegment& segment : segments) {
            readings.push_back(segment.reading);
        }
        return readings;
    }

    std::vector<std::byte> bytes_;
    std::optional<astelio::SystemDictionary> dictionary_;
};

// T-B02-1 (with a small dictionary): the cheapest path is split into segments at content words.
TEST_F(ConverterTest, ConvertsASentenceIntoSegments)
{
    const std::vector<ConvertedSegment> segments = Convert(u"わたしはにほんごです");
    EXPECT_EQ(Best(segments), u"私は日本語です");
    EXPECT_EQ(Readings(segments), (std::vector<std::u16string>{u"わたしは", u"にほんごです"}));
}

TEST_F(ConverterTest, SegmentCandidatesReplaceTheHeadAndKeepTheParticle)
{
    const std::vector<ConvertedSegment> segments = Convert(u"わたしはにほんごです");
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].candidates,
              (std::vector<std::u16string>{u"私は", u"渡しは", u"わたしは", u"ワタシハ"}));
}

TEST_F(ConverterTest, PrefixesStayInTheSegmentOfTheirWord)
{
    const std::vector<ConvertedSegment> segments = Convert(u"おちゃ");
    EXPECT_EQ(Best(segments), u"お茶");
    EXPECT_EQ(segments.size(), 1u);
}

// T-B02-2 (engine part): a resized first segment is kept and the rest is converted again.
TEST_F(ConverterTest, FixedSegmentLengthsAreKept)
{
    std::vector<ConvertedSegment> segments = Convert(u"わたしはにほんごです", {3});
    EXPECT_EQ(Readings(segments), (std::vector<std::u16string>{u"わたし", u"は", u"にほんごです"}));
    EXPECT_EQ(segments[0].candidates.at(0), u"私");

    segments = Convert(u"わたしはにほんごです", {5});
    ASSERT_FALSE(segments.empty());
    EXPECT_EQ(segments[0].reading, u"わたしはに");
}

// R-01: half-width letters are kept as they are and converted together with kana.
TEST_F(ConverterTest, UnknownTextIsKept)
{
    std::vector<ConvertedSegment> segments = Convert(u"iPhoneは");
    EXPECT_EQ(Best(segments), u"iPhoneは");
    ASSERT_EQ(segments.size(), 1u);

    segments = Convert(u"ぬ");
    ASSERT_EQ(segments.size(), 1u);
    EXPECT_EQ(segments[0].candidates, (std::vector<std::u16string>{u"ぬ", u"ヌ"}));
    EXPECT_TRUE(Convert(u"").empty());
}

TEST_F(ConverterTest, SegmentsAlwaysCoverTheWholeReading)
{
    const std::u16string alphabet = u"わたしはにほんごですおちゃぬaB1-";
    std::mt19937 random(20260925);
    for (int round = 0; round < 300; ++round) {
        std::u16string reading;
        const std::size_t length = random() % 16 + 1;
        for (std::size_t i = 0; i < length; ++i) {
            reading.push_back(alphabet[random() % alphabet.size()]);
        }
        std::vector<std::size_t> fixed;
        for (std::size_t total = 0; random() % 2 == 0 && total < length;) {
            fixed.push_back(random() % 4 + 1);
            total += fixed.back();
        }
        const std::vector<ConvertedSegment> segments = Convert(reading, fixed);
        std::u16string joined;
        for (const ConvertedSegment& segment : segments) {
            joined += segment.reading;
            ASSERT_FALSE(segment.candidates.empty());
            EXPECT_NE(std::find(segment.candidates.begin(), segment.candidates.end(), segment.reading),
                      segment.candidates.end());
        }
        EXPECT_EQ(joined, reading);
    }
}

// B-04: predictions start with the conversion of what was typed, then longer words.
TEST_F(ConverterTest, PredictsFromThePrefix)
{
    const std::vector<std::u16string> predictions = astelio::Converter(*dictionary_).Predict(u"わた", 9);
    EXPECT_EQ(predictions, (std::vector<std::u16string>{u"綿", u"私", u"渡し"}));
    EXPECT_EQ(astelio::Converter(*dictionary_).Predict(u"わた", 1).size(), 1u);
    EXPECT_TRUE(astelio::Converter(*dictionary_).Predict(u"ぬ", 9).empty());
}

// T-B09-1, T-B09-2: numbers and dates get their other forms, after the best candidate.
TEST_F(ConverterTest, NumbersAndDatesGetSpecialCandidates)
{
    astelio::Converter converter(*dictionary_);
    converter.SetClock([] { return astelio::LocalTime{2026, 9, 25, 14, 5}; });

    std::vector<ConvertedSegment> segments = converter.Convert(u"1234");
    ASSERT_EQ(segments.size(), 1u);
    const std::vector<std::u16string>& numbers = segments[0].candidates;
    ASSERT_GE(numbers.size(), 5u);
    EXPECT_EQ(numbers[0], u"1234");
    EXPECT_EQ(numbers[1], u"１２３４");
    EXPECT_NE(std::find(numbers.begin(), numbers.end(), u"千二百三十四"), numbers.end());
    EXPECT_NE(std::find(numbers.begin(), numbers.end(), u"1,234"), numbers.end());

    segments = converter.Convert(u"きょうは");
    ASSERT_EQ(segments.size(), 1u);
    const std::vector<std::u16string>& dates = segments[0].candidates;
    EXPECT_EQ(dates[0], u"今日は");
    EXPECT_NE(std::find(dates.begin(), dates.end(), u"2026/09/25は"), dates.end()) << "the particle is kept";
    EXPECT_NE(std::find(dates.begin(), dates.end(), u"9月25日(金)は"), dates.end());
}

TEST(HiraganaToKatakana, ConvertsOnlyHiragana)
{
    EXPECT_EQ(astelio::HiraganaToKatakana(u"ゔぁいおりんーゝA漢"), u"ヴァイオリンーヽA漢");
}

} // namespace
