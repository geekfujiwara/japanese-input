#include "astelio/kana_forms.h"

#include <gtest/gtest.h>

namespace astelio {
namespace {

TEST(KanaForms, HalfWidthKatakanaSplitsVoicedMarks)
{
    EXPECT_EQ(ToHalfWidthKatakana(u"にほん"), u"ﾆﾎﾝ");
    EXPECT_EQ(ToHalfWidthKatakana(u"がっこう"), u"ｶﾞｯｺｳ");
    EXPECT_EQ(ToHalfWidthKatakana(u"パーティー"), u"ﾊﾟｰﾃｨｰ");
    EXPECT_EQ(ToHalfWidthKatakana(u"ゔぁ、「A」。"), u"ｳﾞｧ､｢A｣｡");
}

TEST(KanaForms, FullWidthAscii)
{
    EXPECT_EQ(ToFullWidthAscii(u"nihon 1!"), u"ｎｉｈｏｎ\u3000１！");
    EXPECT_EQ(ToFullWidthAscii(u"かな"), u"かな");
}

TEST(KanaForms, KanaToRomajiTypesBackToTheSameKana)
{
    const RomajiTable& table = RomajiTable::Default();
    EXPECT_EQ(KanaToRomaji(u"にほん", table), u"nihon");
    EXPECT_EQ(KanaToRomaji(u"しんぶん", table), u"shinbun");
    EXPECT_EQ(KanaToRomaji(u"きって", table), u"kitte");
    EXPECT_EQ(KanaToRomaji(u"ちゃいろ", table), u"chairo");
    EXPECT_EQ(KanaToRomaji(u"ふじ", table), u"fuji");
    EXPECT_EQ(KanaToRomaji(u"かんい", table), u"kanni");
    EXPECT_EQ(KanaToRomaji(u"ほんや", table), u"honnya");
    EXPECT_EQ(KanaToRomaji(u"あっ", table), u"axtu");
    EXPECT_EQ(KanaToRomaji(u"iPhoneを", table), u"iPhonewo");
    EXPECT_EQ(KanaToRomaji(u"ゆーざー、でーた。", table), u"yu-za-,de-ta.") << "long vowels and punctuation";
}

} // namespace
} // namespace astelio
