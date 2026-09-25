#include "evaluation.h"

#include "astelio/dictionary_builder.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace astelio::eval {
namespace {

TEST(Corpus, ParsesEntriesAndSkipsComments)
{
    std::vector<CorpusError> errors;
    const std::vector<CorpusEntry> entries = ParseCorpus(
        "# category\treading\texpected\talternatives\n"
        "日常\tわたしは\t私は\t渡しは|わたしは\r\n"
        "\n"
        "IT・技術\tiPhoneをかう\tiPhoneを買う\n",
        errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].category, "日常");
    EXPECT_EQ(entries[0].reading, u"わたしは");
    EXPECT_EQ(entries[0].expected, u"私は");
    EXPECT_EQ(entries[0].alternatives, (std::vector<std::u16string>{u"渡しは", u"わたしは"}));
    EXPECT_EQ(entries[0].line, 2u);
    EXPECT_TRUE(entries[1].alternatives.empty());
    EXPECT_EQ(entries[1].line, 4u);
}

TEST(Corpus, ReportsMalformedLines)
{
    std::vector<CorpusError> errors;
    const std::vector<CorpusEntry> entries = ParseCorpus("日常\tわたし\n日常\t\t私\n日常\tわたし\t私\t\t余分\n", errors);
    EXPECT_TRUE(entries.empty());
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_EQ(errors[0].line, 1u);
    EXPECT_EQ(errors[1].line, 2u);
    EXPECT_EQ(errors[2].line, 3u);
}

class EvaluationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        constexpr std::uint16_t kIds = 3; // 0 edge, 1 noun, 2 particle
        ConnectionMatrix matrix;
        matrix.size = kIds;
        matrix.costs.assign(kIds * kIds, 100);
        matrix.word_types = {WordType::Edge, WordType::Content, WordType::Suffix};
        matrix.unknown_id = 1;
        matrix.unknown_cost = 5000;
        matrix.costs[0 * kIds + 1] = 0;
        matrix.costs[1 * kIds + 2] = 0;
        matrix.costs[2 * kIds + 1] = 0;
        matrix.costs[2 * kIds + 0] = 0;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"わたし", u"私", 1, 1, 0, 300});
        builder.Add({u"わたし", u"渡し", 1, 1, 0, 900});
        builder.Add({u"は", u"は", 2, 2, 0, 50});
        builder.Add({u"はし", u"箸", 1, 1, 0, 300});
        builder.Add({u"はし", u"橋", 1, 1, 0, 400});
        builder.Add({u"を", u"を", 2, 2, 0, 50});
        builder.Add({u"わたる", u"渡る", 1, 1, 0, 300});
        bytes_ = builder.Build();
        dictionary_ = SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
        converter_.emplace(*dictionary_);
    }

    static CorpusEntry Entry(std::u16string reading, std::u16string expected,
                             std::vector<std::u16string> alternatives = {})
    {
        CorpusEntry entry;
        entry.category = "test";
        entry.reading = std::move(reading);
        entry.expected = std::move(expected);
        entry.alternatives = std::move(alternatives);
        return entry;
    }

    std::vector<std::byte> bytes_;
    std::optional<SystemDictionary> dictionary_;
    std::optional<Converter> converter_;
};

TEST_F(EvaluationTest, TheFirstCandidateMustMatch)
{
    const EntryResult hit = Evaluate(*converter_, Entry(u"わたしは", u"私は"));
    EXPECT_EQ(hit.best, u"私は");
    EXPECT_TRUE(hit.correct);
    EXPECT_TRUE(hit.top5);

    const EntryResult alternative = Evaluate(*converter_, Entry(u"わたしは", u"わたしは", {u"私は"}));
    EXPECT_TRUE(alternative.correct) << "an accepted alternative counts";
}

TEST_F(EvaluationTest, TopFiveKeepsTheSegmentsAndPicksCandidates)
{
    const EntryResult result = Evaluate(*converter_, Entry(u"はしをわたる", u"橋を渡る"));
    EXPECT_EQ(result.best, u"箸を渡る");
    EXPECT_FALSE(result.correct);
    EXPECT_TRUE(result.top5) << "橋 is the second candidate of the first segment";

    EXPECT_FALSE(Evaluate(*converter_, Entry(u"はしをわたる", u"端を渡る")).top5);
}

TEST(Summary, CountsPerCategory)
{
    Summary summary;
    CorpusEntry daily;
    daily.category = "日常";
    CorpusEntry business;
    business.category = "ビジネス";
    summary.Add(daily, {u"", true, true});
    summary.Add(daily, {u"", false, true});
    summary.Add(business, {u"", false, false});
    EXPECT_EQ(summary.overall.total, 3u);
    EXPECT_EQ(summary.overall.correct, 1u);
    EXPECT_EQ(summary.overall.top5, 2u);
    EXPECT_DOUBLE_EQ(summary.categories["日常"].Accuracy(), 50.0);
    EXPECT_DOUBLE_EQ(summary.categories["ビジネス"].Top5Accuracy(), 0.0);

    const std::string report = FormatReport(summary);
    EXPECT_NE(report.find("| 日常 | 2 | 50.0% | 100.0% |"), std::string::npos) << report;
    EXPECT_NE(report.find("| **全体** | 3 | 33.3% | 66.7% |"), std::string::npos) << report;
}

// The CI gate: 0.5 points or more below the baseline fails.
TEST(Summary, RegressionGate)
{
    EXPECT_FALSE(Regressed(62.0, 62.0));
    EXPECT_FALSE(Regressed(61.6, 62.0));
    EXPECT_TRUE(Regressed(61.5, 62.0));
    EXPECT_TRUE(Regressed(50.0, 62.0));
    EXPECT_FALSE(Regressed(70.0, 62.0));
}

} // namespace
} // namespace astelio::eval
