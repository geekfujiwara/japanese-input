#include "astelio/user_dictionary.h"
#include "astelio/user_dictionary_io.h"

#include <gtest/gtest.h>

namespace astelio {
namespace {

using Pos = UserDictionary::PartOfSpeech;

// The shared contract of D-02 / D-03 / D-08: the part of speech names go both ways.
TEST(UserDictionaryContract, PartOfSpeechNamesRoundTrip)
{
    for (std::size_t i = 0; i < UserDictionary::kPartOfSpeechNames.size(); ++i) {
        const auto pos = static_cast<Pos>(i);
        EXPECT_EQ(UserDictionary::PartOfSpeechFromName(UserDictionary::PartOfSpeechName(pos)), pos);
    }
    EXPECT_EQ(std::u16string(UserDictionary::PartOfSpeechName(Pos::Suppressed)), u"抑制単語");
    EXPECT_FALSE(UserDictionary::PartOfSpeechFromName(u"動詞").has_value());
}

TEST(UserDictionaryContract, ValidWords)
{
    EXPECT_TRUE(UserDictionary::Valid({u"あすてりお", u"Astelio", Pos::ProperNoun, u""}));
    EXPECT_TRUE(UserDictionary::Valid({u"", u"蛙", Pos::Suppressed, u""})) << "suppressed for any reading";
    EXPECT_FALSE(UserDictionary::Valid({u"", u"蛙", Pos::Noun, u""}));
    EXPECT_FALSE(UserDictionary::Valid({u"かえる", u"", Pos::Noun, u""}));
    EXPECT_FALSE(UserDictionary::Valid({u"か\tえる", u"蛙", Pos::Noun, u""}));
    EXPECT_FALSE(UserDictionary::Valid({u"かえる", u"蛙", Pos::Noun, u"一行目\n二行目"}));
    EXPECT_FALSE(UserDictionary::Valid(
        {std::u16string(UserDictionary::kMaxReadingLength + 1, u'あ'), u"蛙", Pos::Noun, u""}));
}

} // namespace
} // namespace astelio
