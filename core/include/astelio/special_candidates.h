#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astelio {

struct LocalTime {
    int year = 2000;
    int month = 1; // 1-12
    int day = 1;
    int hour = 0;
    int minute = 0;
};

LocalTime CurrentLocalTime();

// B-09: other ways to write a run of half-width digits ("1234" -> １２３４, 千二百三十四, 一二三四, 1,234).
// Empty when `digits` is not all ASCII digits.
std::vector<std::u16string> NumberForms(std::u16string_view digits);
std::u16string ToKanjiNumber(std::u16string_view digits); // "1234" -> "千二百三十四"

// B-09: dates and times for words such as きょう, あした, いま. Empty for other readings.
// `preferred` (C-13) comes first among the dates; the others keep the order of DateFormat.
enum class DateFormat : std::uint8_t {
    SlashPadded,     // 2026/09/25
    Kanji,           // 2026年9月25日
    MonthDay,        // 9月25日
    MonthDayWeekday, // 9月25日(金)
    Era,             // 令和8年9月25日
    Iso,             // 2026-09-25
    Compact,         // 20260925
    SlashShort,      // 9/25
};
inline constexpr std::array<std::u16string_view, 8> kDateFormatNames = {
    u"yyyy/MM/dd", u"yyyy年M月d日", u"M月d日", u"M月d日(ddd)", u"令和", u"yyyy-MM-dd", u"yyyyMMdd", u"M/d",
};
std::optional<DateFormat> DateFormatFromName(std::u16string_view name);

std::vector<std::u16string> DateForms(std::u16string_view reading, const LocalTime& now,
                                      DateFormat preferred = DateFormat::SlashPadded);
bool IsDateReading(std::u16string_view reading);

} // namespace astelio
