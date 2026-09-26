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
        matrix.size = 3;
        matrix.costs.assign(9, 0);
        matrix.word_types = {WordType::Edge, WordType::Content, WordType::Suffix};
        matrix.unknown_id = 1;
        DictionaryBuilder builder(std::move(matrix));
        builder.Add({u"ゆーざー", u"ユーザー", 1, 1, 0, 300});
        builder.Add({u"てんき", u"天気", 1, 1, 0, 300});
        builder.Add({u"てんき", u"転機", 1, 1, 0, 800});
        builder.Add({u"でんき", u"電気", 1, 1, 0, 400});
        builder.Add({u"かいぎしつ", u"会議室", 1, 1, 0, 500});
        builder.Add({u"かいぎしる", u"会議汁", 1, 1, 0, 3000}); // a rare word, for the margin
        builder.Add({u"でんしゃ", u"電車", 1, 1, 0, 300});
        builder.Add({u"れんらく", u"連絡", 1, 1, 0, 500});
        builder.Add({u"れんだこ", u"連だこ", 2, 2, 0, 0}); // a fragment
        builder.Add({u"すまーとふぉん", u"スマートフォン", 1, 1, 0, 600});
        builder.Add({u"いんたーねっと", u"インターネット", 1, 1, 0, 500});
        builder.Add({u"しりょう", u"資料", 1, 1, 0, 300});
        builder.Add({u"しちょう", u"市長", 1, 1, 0, 350});
        bytes_ = builder.Build();
        dictionary_ = SystemDictionary::Open(bytes_);
        ASSERT_TRUE(dictionary_);
    }

    std::vector<TypoCandidate> Find(std::u16string_view keys, std::size_t limit = 5)
    {
        return FindTypoCandidates(keys, RomajiTable::Default(), *dictionary_, limit);
    }

    std::optional<TypoCandidate> Suggest(std::u16string_view keys)
    {
        return SuggestTypoCorrection(keys, RomajiTable::Default(), *dictionary_);
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

// T-B14-2
TEST_F(TypoCandidatesTest, FindsVowelSimilarSoundAndDoubledKeySlips)
{
    EXPECT_EQ(Find(u"dansya").at(0).surface, u"電車") << "a for e";
    EXPECT_EQ(Find(u"dansya").at(0).cost, 300 + kTypoSoundPenalty);
    EXPECT_EQ(Find(u"suma-tohon").at(0).surface, u"スマートフォン") << "h for f";
    EXPECT_EQ(Find(u"inta-nerro").at(0).surface, u"インターネット") << "rr for tt";
    EXPECT_EQ(Find(u"inta-nerro").at(0).keys, u"inta-netto");
}

// T-B14-3
TEST_F(TypoCandidatesTest, SuggestsWhenTheReadingIsNotAWord)
{
    const std::optional<TypoCandidate> suggested = Suggest(u"yu-a-");
    ASSERT_TRUE(suggested);
    EXPECT_EQ(suggested->surface, u"ユーザー");
    EXPECT_FALSE(Suggest(u"yu-za-")) << "a correct reading";
    EXPECT_FALSE(Suggest(u"tenki"));
    EXPECT_FALSE(Suggest(u"xyzxyz"));
}

TEST_F(TypoCandidatesTest, SkipsFragmentsAndParticles)
{
    ASSERT_EQ(Find(u"renrako").at(0).surface, u"連だこ") << "the fragment is the cheapest candidate";
    const std::optional<TypoCandidate> suggested = Suggest(u"renrako");
    ASSERT_TRUE(suggested);
    EXPECT_EQ(suggested->surface, u"連絡");
}

TEST_F(TypoCandidatesTest, SuggestsOverAWordOnlyWhenMuchMoreLikely)
{
    EXPECT_FALSE(Suggest(u"sityou")) << "市長 is a common word; 資料 is not much more likely";
    const std::optional<TypoCandidate> suggested = Suggest(u"kaigisiru");
    ASSERT_TRUE(suggested) << "会議汁 is rare";
    EXPECT_EQ(suggested->surface, u"会議室");
}

TEST(TypoKeys, NeighboursFollowTheUsLayout)
{
    EXPECT_EQ(NeighbouringKeys(u'g'), u"ftyhbv");
    EXPECT_EQ(NeighbouringKeys(u'-'), u"p");
    EXPECT_TRUE(NeighbouringKeys(u'1').empty());
}

} // namespace
} // namespace astelio
