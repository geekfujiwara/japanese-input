#include "astelio/user_dictionary.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace astelio {
namespace {

using Pos = UserDictionary::PartOfSpeech;
using Word = UserDictionary::Word;

TEST(UserDictionary, AddReplacesTheSameWord)
{
    UserDictionary dictionary;
    EXPECT_TRUE(dictionary.Empty());
    EXPECT_TRUE(dictionary.Add({u"あすてりお", u"Astelio", Pos::ProperNoun, u""}));
    EXPECT_TRUE(dictionary.Add({u"あすてりお", u"アステリオ", Pos::Noun, u""}));
    EXPECT_TRUE(dictionary.Add({u"あすてりお", u"Astelio", Pos::ProperNoun, u"IMEの名前"}));
    ASSERT_EQ(dictionary.Words().size(), 2u);
    EXPECT_EQ(dictionary.Words()[0].comment, u"IMEの名前") << "replaced in place";
    EXPECT_EQ(dictionary.Words()[1].surface, u"アステリオ");

    EXPECT_FALSE(dictionary.Add({u"", u"Astelio", Pos::Noun, u""}));
    EXPECT_FALSE(dictionary.Add({u"あ\tす", u"Astelio", Pos::Noun, u""}));
    EXPECT_EQ(dictionary.Words().size(), 2u);
}

TEST(UserDictionary, AtMostMaxWords)
{
    UserDictionary dictionary;
    for (std::size_t i = 0; i < UserDictionary::kMaxWords; ++i) {
        ASSERT_TRUE(dictionary.Add({u"よみ", u"語" + std::u16string(1, static_cast<char16_t>(0x4E00 + i)),
                                    Pos::Noun, u""}));
    }
    EXPECT_FALSE(dictionary.Add({u"よみ", u"あふれる", Pos::Noun, u""}));
    EXPECT_EQ(dictionary.Words().size(), UserDictionary::kMaxWords);
    dictionary.Clear();
    EXPECT_TRUE(dictionary.Empty());
}

TEST(UserDictionary, UpdateAndRemove)
{
    UserDictionary dictionary;
    const Word first{u"かえる", u"蛙", Pos::Noun, u""};
    const Word second{u"いぬ", u"犬", Pos::Noun, u""};
    ASSERT_TRUE(dictionary.Add(first));
    ASSERT_TRUE(dictionary.Add(second));

    const Word edited{u"かえる", u"カエル", Pos::Noun, u"片仮名"};
    EXPECT_TRUE(dictionary.Update(first, edited));
    EXPECT_EQ(dictionary.Words(), (std::vector<Word>{edited, second})) << "the order is kept";
    EXPECT_FALSE(dictionary.Update(first, edited)) << "the old word is gone";
    EXPECT_FALSE(dictionary.Update(edited, {u"", u"カエル", Pos::Noun, u""})) << "not valid";
    EXPECT_EQ(dictionary.Words().front(), edited);

    EXPECT_TRUE(dictionary.Remove(edited));
    EXPECT_FALSE(dictionary.Remove(edited));
    EXPECT_EQ(dictionary.Words(), (std::vector<Word>{second}));
}

TEST(UserDictionary, SearchMatchesReadingSurfaceAndComment)
{
    UserDictionary dictionary;
    ASSERT_TRUE(dictionary.Add({u"あすてりお", u"Astelio", Pos::ProperNoun, u"IME"}));
    ASSERT_TRUE(dictionary.Add({u"ふじわら", u"藤原", Pos::PersonName, u"作者"}));
    ASSERT_TRUE(dictionary.Add({u"", u"蛙", Pos::Suppressed, u""}));

    EXPECT_EQ(dictionary.Search(u"てり").size(), 1u);
    EXPECT_EQ(dictionary.Search(u"藤").size(), 1u);
    EXPECT_EQ(dictionary.Search(u"作者").front().surface, u"藤原");
    EXPECT_TRUE(dictionary.Search(u"猫").empty());
    EXPECT_EQ(dictionary.Search(u"").size(), 3u);
}

TEST(UserDictionary, LookupAndPredictLeaveSuppressedWordsOut)
{
    UserDictionary dictionary;
    ASSERT_TRUE(dictionary.Add({u"かえる", u"カエル", Pos::Noun, u""}));
    ASSERT_TRUE(dictionary.Add({u"かえるくん", u"かえるくん", Pos::PersonName, u""}));
    ASSERT_TRUE(dictionary.Add({u"かえる", u"変える", Pos::Suppressed, u""}));
    ASSERT_TRUE(dictionary.Add({u"かえる", u"帰る", Pos::Noun, u""}));
    ASSERT_TRUE(dictionary.Add({u"", u"帰る", Pos::Suppressed, u""}));

    EXPECT_EQ(dictionary.Lookup(u"かえる"), (std::vector<std::u16string>{u"カエル"}))
        << "a suppressed word hides an added one too";
    EXPECT_TRUE(dictionary.Lookup(u"かえ").empty());
    EXPECT_EQ(dictionary.Predict(u"かえ", 10), (std::vector<std::u16string>{u"カエル", u"かえるくん"}));
    EXPECT_EQ(dictionary.Predict(u"かえ", 1).size(), 1u);
    EXPECT_TRUE(dictionary.Predict(u"いぬ", 10).empty());
}

TEST(UserDictionary, SuppressedForTheReadingOrForAny)
{
    UserDictionary dictionary;
    ASSERT_TRUE(dictionary.Add({u"かえる", u"変える", Pos::Suppressed, u""}));
    ASSERT_TRUE(dictionary.Add({u"", u"蛙", Pos::Suppressed, u""}));
    ASSERT_TRUE(dictionary.Add({u"いぬ", u"犬", Pos::Noun, u""}));

    EXPECT_TRUE(dictionary.Suppressed(u"かえる", u"変える"));
    EXPECT_FALSE(dictionary.Suppressed(u"かわる", u"変える"));
    EXPECT_TRUE(dictionary.Suppressed(u"", u"変える")) << "an unknown reading matches";
    EXPECT_TRUE(dictionary.Suppressed(u"かえる", u"蛙"));
    EXPECT_TRUE(dictionary.Suppressed(u"あ", u"蛙")) << "suppressed for any reading";
    EXPECT_FALSE(dictionary.Suppressed(u"いぬ", u"犬")) << "an added word is not suppressed";
}

TEST(UserDictionary, SerializeAndParseRoundTrip)
{
    UserDictionary dictionary;
    ASSERT_TRUE(dictionary.Add({u"あすてりお", u"Astelio", Pos::ProperNoun, u"IMEの名前"}));
    ASSERT_TRUE(dictionary.Add({u"よろ", u"よろしくお願いします", Pos::Abbreviation, u""}));
    ASSERT_TRUE(dictionary.Add({u"", u"蛙", Pos::Suppressed, u""}));
    ASSERT_TRUE(dictionary.Add({u"にこ", u"😄", Pos::Symbol, u""}));

    const std::string text = dictionary.Serialize();
    EXPECT_EQ(text.rfind("# astelio user dictionary 1\n", 0), 0u);
    EXPECT_NE(text.find("\xE3\x82\x88\xE3\x82\x8D\t"), std::string::npos) << "UTF-8, tab separated";
    EXPECT_EQ(UserDictionary::Parse(text).Words(), dictionary.Words());
    EXPECT_TRUE(UserDictionary::Parse("").Empty());
    EXPECT_TRUE(UserDictionary().Serialize().rfind("# astelio user dictionary 1", 0) == 0);
}

TEST(UserDictionary, ParseSkipsBrokenLines)
{
    const std::string text = "# astelio user dictionary 1\r\n"
                             "\xE3\x81\x84\xE3\x81\xAC\t\xE7\x8A\xAC\t\xE5\x90\x8D\xE8\xA9\x9E\t\r\n" // いぬ 犬 名詞
                             "\n"
                             "only one column\n"
                             "\xE3\x81\x84\xE3\x81\xAC\t\xE7\x8A\xAC\t\xE5\x8B\x95\xE8\xA9\x9E\t\n" // 動詞: unknown
                             "\t\xE7\x8A\xAC\t\xE5\x90\x8D\xE8\xA9\x9E\t\n"                         // no reading
                             "\xFF\xFE\t\xE7\x8A\xAC\t\xE5\x90\x8D\xE8\xA9\x9E\t\n"                 // not UTF-8
                             "a\tb\t\xE5\x90\x8D\xE8\xA9\x9E\tc\td\n"                               // five columns
                             "\xE3\x81\xAD\xE3\x81\x93\t\xE7\x8C\xAB\t\xE5\x90\x8D\xE8\xA9\x9E\t\xE3\x81\xAB\xE3\x82\x83\n";
    const UserDictionary dictionary = UserDictionary::Parse(text);
    EXPECT_EQ(dictionary.Words(), (std::vector<Word>{{u"いぬ", u"犬", Pos::Noun, u""},
                                                     {u"ねこ", u"猫", Pos::Noun, u"にゃ"}}));
}

TEST(UserDictionary, ParseKeepsAtMostMaxWords)
{
    std::string text = "# astelio user dictionary 1\n";
    for (std::size_t i = 0; i < UserDictionary::kMaxWords + 5; ++i) {
        text += "\xE3\x82\x88\xE3\x81\xBF\tw" + std::to_string(i) + "\t\xE5\x90\x8D\xE8\xA9\x9E\t\n";
    }
    EXPECT_EQ(UserDictionary::Parse(text).Words().size(), UserDictionary::kMaxWords);
}

} // namespace
} // namespace astelio
