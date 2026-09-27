#include "astelio/special_candidates.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace astelio {
namespace {

bool Contains(const std::vector<std::u16string>& list, std::u16string_view text)
{
    return std::find(list.begin(), list.end(), text) != list.end();
}

TEST(SpecialCandidates, KanjiNumbers)
{
    EXPECT_EQ(ToKanjiNumber(u"1234"), u"千二百三十四");
    EXPECT_EQ(ToKanjiNumber(u"11"), u"十一");
    EXPECT_EQ(ToKanjiNumber(u"1000"), u"千");
    EXPECT_EQ(ToKanjiNumber(u"10000"), u"一万");
    EXPECT_EQ(ToKanjiNumber(u"20050"), u"二万五十");
    EXPECT_EQ(ToKanjiNumber(u"100000000"), u"一億");
    EXPECT_EQ(ToKanjiNumber(u"12345678"), u"千二百三十四万五千六百七十八");
    EXPECT_EQ(ToKanjiNumber(u"007"), u"七");
    EXPECT_EQ(ToKanjiNumber(u"0"), u"〇");
    EXPECT_EQ(ToKanjiNumber(u"12345678901234567"), u"") << "more than 16 digits";
}

// T-B09-1
TEST(SpecialCandidates, NumberForms)
{
    EXPECT_EQ(NumberForms(u"1234"), (std::vector<std::u16string>{u"１２３４", u"千二百三十四", u"一二三四", u"1,234"}));
    EXPECT_EQ(NumberForms(u"12"), (std::vector<std::u16string>{u"１２", u"十二", u"一二"}));
    EXPECT_EQ(NumberForms(u"1234567").back(), u"1,234,567");
    EXPECT_TRUE(NumberForms(u"12a").empty());
    EXPECT_TRUE(NumberForms(u"").empty());
}

// T-B09-2 (clock fixed at 2026-09-25 14:05, a Friday)
TEST(SpecialCandidates, DatesAndTimes)
{
    const LocalTime now{2026, 9, 25, 14, 5};
    const std::vector<std::u16string> today = DateForms(u"きょう", now);
    EXPECT_TRUE(Contains(today, u"2026/09/25"));
    EXPECT_TRUE(Contains(today, u"2026年9月25日"));
    EXPECT_TRUE(Contains(today, u"9月25日(金)"));
    EXPECT_TRUE(Contains(today, u"令和8年9月25日"));
    EXPECT_TRUE(Contains(today, u"金曜日"));

    EXPECT_TRUE(Contains(DateForms(u"あした", now), u"9月26日(土)"));
    EXPECT_TRUE(Contains(DateForms(u"きのう", now), u"9月24日(木)"));
    EXPECT_TRUE(Contains(DateForms(u"あした", LocalTime{2026, 12, 31, 0, 0}), u"2027/01/01"));
    EXPECT_TRUE(Contains(DateForms(u"あした", LocalTime{2028, 2, 28, 0, 0}), u"2028/02/29")) << "leap year";
    EXPECT_TRUE(Contains(DateForms(u"きょう", LocalTime{2019, 5, 1, 0, 0}), u"令和元年5月1日"));

    EXPECT_EQ(DateForms(u"いま", now), (std::vector<std::u16string>{u"14:05", u"14時05分", u"午後2時05分"}));
    EXPECT_TRUE(DateForms(u"いぬ", now).empty());
    EXPECT_TRUE(IsDateReading(u"あさって"));
    EXPECT_FALSE(IsDateReading(u"あさ"));
}

// T-B09-3, T-C13-1: yyyy-MM-dd, yyyyMMdd and M/d, and the preferred form first among the dates
TEST(SpecialCandidates, DateFormats)
{
    const LocalTime now{2026, 9, 5, 14, 5};
    EXPECT_EQ(DateForms(u"きょう", now),
              (std::vector<std::u16string>{u"2026/09/05", u"2026年9月5日", u"9月5日", u"9月5日(土)", u"令和8年9月5日",
                                           u"2026-09-05", u"20260905", u"9/5", u"土曜日"}));
    const std::vector<std::u16string> iso = DateForms(u"あした", now, DateFormat::Iso);
    EXPECT_EQ(iso.front(), u"2026-09-06");
    EXPECT_EQ(iso.size(), 9u) << "the others are still there";
    EXPECT_EQ(DateForms(u"きょう", now, DateFormat::Compact).front(), u"20260905");
    EXPECT_EQ(DateForms(u"きょう", now, DateFormat::SlashShort).front(), u"9/5");
    EXPECT_EQ(DateForms(u"きょう", LocalTime{2019, 4, 30, 0, 0}, DateFormat::Era).front(), u"2019/04/30")
        << "no era form before 令和; the usual order";

    for (std::size_t i = 0; i < kDateFormatNames.size(); ++i) {
        EXPECT_EQ(DateFormatFromName(kDateFormatNames[i]), static_cast<DateFormat>(i));
    }
    EXPECT_FALSE(DateFormatFromName(u"dd.MM.yyyy").has_value());
}

} // namespace
} // namespace astelio
