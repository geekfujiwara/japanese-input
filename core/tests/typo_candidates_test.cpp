#include "astelio/typo_candidates.h"

#include "astelio/dictionary_builder.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace astelio {
namespace {

class TypoCandidatesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ConnectionMatrix matrix;
        matrix.size = 2;
        matrix.costs.assign(4, 0);
        matrix.word_types = {WordType::Edge, WordType::Content};
        matrix.unknown_id = 1;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"ゆーざー", u"ユーザー", 1, 1, 0, 300});
        builder.Add({u"てんき", u"天気", 1, 1, 0, 300});
        builder.Add({u"てんき", u"転機", 1, 1, 0, 800});
        builder.Add({u"でんき", u"電気", 1, 1, 0, 400});
        builder.Add({u"かいぎしつ", u"会議室", 1, 1, 0, 500});
        bytes_ = builder.Build();
        dictionary_ = SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
    }

    std::vector<TypoCandidate> Find(std::u16string_view keys, std::size_t limit = 5)
    {
        return FindTypoCandidates(keys, RomajiTable::Default(), *dictionary_, limit);
    }

    std::vector<std::byte> bytes_;
    std::optional<SystemDictionary> dictionary_;
};

// T-B14-2
TEST_F(TypoCandidatesTest, FindsTheWordAfterAMissingKey)
{
    const std::vector<TypoCandidate> found = Find(u"yu-a-");
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(found[0].surface, u"ユーザー");
    EXPECT_EQ(found[0].reading, u"ゆーざー");
    EXPECT_EQ(found[0].keys, u"yu-za-");
    EXPECT_EQ(found[0].cost, 300 + kTypoKeyPenalty);
}

TEST_F(TypoCandidatesTest, FindsNeighbouringExtraAndSwappedKeys)
{
    EXPECT_EQ(Find(u"renki").at(0).surface, u"天気") << "r is next to t";
    EXPECT_EQ(Find(u"tenkki").at(0).surface, u"天気") << "an extra key";
    EXPECT_EQ(Find(u"kaigisiru").at(0).surface, u"会議室");
    EXPECT_EQ(Find(u"kaigsiitu").at(0).surface, u"会議室") << "swapped keys";
}

TEST_F(TypoCandidatesTest, NeverOffersTheReadingAsTyped)
{
    const std::vector<TypoCandidate> found = Find(u"tenki");
    for (const TypoCandidate& candidate : found) {
        EXPECT_NE(candidate.reading, u"てんき");
    }
    EXPECT_TRUE(found.empty()) << "電気 needs d, which is not next to t";
    EXPECT_EQ(Find(u"fenki").at(0).surface, u"天気") << "f is next to both t and d; 天気 is cheaper";
    EXPECT_EQ(Find(u"fenki").at(1).surface, u"電気");
    EXPECT_TRUE(Find(u"xyzxyz").empty());
    EXPECT_TRUE(Find(u"").empty());
    EXPECT_EQ(Find(u"tenki", 0).size(), 0u);
}

TEST(TypoKeys, NeighboursFollowTheUsLayout)
{
    EXPECT_EQ(NeighbouringKeys(u'g'), u"ftyhbv");
    EXPECT_EQ(NeighbouringKeys(u'-'), u"p");
    EXPECT_TRUE(NeighbouringKeys(u'1').empty());
}

} // namespace
} // namespace astelio
