#pragma once

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
std::vector<std::u16string> DateForms(std::u16string_view reading, const LocalTime& now);
bool IsDateReading(std::u16string_view reading);

} // namespace astelio
