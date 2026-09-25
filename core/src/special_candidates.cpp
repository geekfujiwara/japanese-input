#include "astelio/special_candidates.h"

#include <ctime>
#include <string>

namespace astelio {
namespace {

constexpr char16_t kKanjiDigits[] = u"〇一二三四五六七八九";

std::u16string Ascii(const std::string& text)
{
    return std::u16string(text.begin(), text.end());
}

std::u16string Number(int value, int width = 0)
{
    std::string text = std::to_string(value);
    while (static_cast<int>(text.size()) < width) {
        text.insert(text.begin(), '0');
    }
    return Ascii(text);
}

bool AllDigits(std::u16string_view text)
{
    if (text.empty()) {
        return false;
    }
    for (const char16_t c : text) {
        if (c < u'0' || c > u'9') {
            return false;
        }
    }
    return true;
}

// Days since 1970-01-01 (proleptic Gregorian), after Howard Hinnant's civil-date algorithms.
int DaysFromCivil(int year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int year_of_era = year - era * 400;
    const int day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

void CivilFromDays(int days, int& year, int& month, int& day)
{
    days += 719468;
    const int era = (days >= 0 ? days : days - 146096) / 146097;
    const int day_of_era = days - era * 146097;
    const int year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int shifted_month = (5 * day_of_year + 2) / 153;
    day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
}

struct DateWord {
    std::u16string_view reading;
    int offset; // days from today
};

constexpr DateWord kDateWords[] = {
    {u"きょう", 0},   {u"ほんじつ", 0},   {u"あした", 1},   {u"あす", 1},        {u"みょうにち", 1},
    {u"あさって", 2}, {u"きのう", -1},    {u"さくじつ", -1}, {u"おととい", -2},
};

bool IsTimeReading(std::u16string_view reading)
{
    return reading == u"いま" || reading == u"げんざい";
}

} // namespace

LocalTime CurrentLocalTime()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    LocalTime time;
    time.year = parts.tm_year + 1900;
    time.month = parts.tm_mon + 1;
    time.day = parts.tm_mday;
    time.hour = parts.tm_hour;
    time.minute = parts.tm_min;
    return time;
}

std::u16string ToKanjiNumber(std::u16string_view digits)
{
    std::size_t first = 0;
    while (first + 1 < digits.size() && digits[first] == u'0') {
        ++first;
    }
    digits.remove_prefix(first);
    if (!AllDigits(digits) || digits.size() > 16) {
        return {};
    }
    if (digits == u"0") {
        return u"〇";
    }
    static constexpr std::u16string_view kGroups[] = {u"", u"万", u"億", u"兆"};
    static constexpr std::u16string_view kPlaces[] = {u"", u"十", u"百", u"千"};
    std::u16string result;
    const std::size_t count = digits.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t power = count - 1 - i;
        const int digit = digits[i] - u'0';
        const std::size_t place = power % 4;
        if (digit != 0) {
            if (digit != 1 || place == 0) {
                result.push_back(kKanjiDigits[digit]);
            }
            result += kPlaces[place];
        }
        if (place == 0) {
            // Name the group (万, 億, 兆) only when one of its four digits is not zero.
            const std::size_t group_begin = i >= 3 ? i - 3 : 0;
            bool nonzero = false;
            for (std::size_t k = group_begin; k <= i; ++k) {
                nonzero = nonzero || digits[k] != u'0';
            }
            if (nonzero) {
                result += kGroups[power / 4];
            }
        }
    }
    return result;
}

std::vector<std::u16string> NumberForms(std::u16string_view digits)
{
    std::vector<std::u16string> forms;
    if (!AllDigits(digits)) {
        return forms;
    }
    std::u16string full;
    std::u16string kanji_digits;
    for (const char16_t c : digits) {
        full.push_back(static_cast<char16_t>(c + 0xFEE0));
        kanji_digits.push_back(kKanjiDigits[c - u'0']);
    }
    forms.push_back(full);
    if (std::u16string kanji = ToKanjiNumber(digits); !kanji.empty() && kanji != kanji_digits) {
        forms.push_back(std::move(kanji));
    }
    forms.push_back(kanji_digits);
    if (digits.size() > 3 && digits.front() != u'0') {
        std::u16string grouped;
        for (std::size_t i = 0; i < digits.size(); ++i) {
            if (i > 0 && (digits.size() - i) % 3 == 0) {
                grouped.push_back(u',');
            }
            grouped.push_back(digits[i]);
        }
        forms.push_back(std::move(grouped));
    }
    return forms;
}

std::vector<std::u16string> DateForms(std::u16string_view reading, const LocalTime& now)
{
    std::vector<std::u16string> forms;
    if (IsTimeReading(reading)) {
        const int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
        forms.push_back(Number(now.hour) + u":" + Number(now.minute, 2));
        forms.push_back(Number(now.hour) + u"時" + Number(now.minute, 2) + u"分");
        forms.push_back((now.hour < 12 ? u"午前" : u"午後") + Number(hour12) + u"時" + Number(now.minute, 2) + u"分");
        return forms;
    }
    for (const DateWord& word : kDateWords) {
        if (reading != word.reading) {
            continue;
        }
        const int days = DaysFromCivil(now.year, now.month, now.day) + word.offset;
        int year = 0;
        int month = 0;
        int day = 0;
        CivilFromDays(days, year, month, day);
        static constexpr std::u16string_view kWeekdays[] = {u"日", u"月", u"火", u"水", u"木", u"金", u"土"};
        const std::u16string_view weekday = kWeekdays[((days % 7) + 7 + 4) % 7]; // 1970-01-01 was a Thursday
        const std::u16string month_day = Number(month) + u"月" + Number(day) + u"日";
        forms.push_back(Number(year) + u"/" + Number(month, 2) + u"/" + Number(day, 2));
        forms.push_back(Number(year) + u"年" + month_day);
        forms.push_back(month_day);
        forms.push_back(month_day + u"(" + std::u16string(weekday) + u")");
        if (DaysFromCivil(year, month, day) >= DaysFromCivil(2019, 5, 1)) {
            const int reiwa = year - 2018;
            forms.push_back(u"令和" + (reiwa == 1 ? std::u16string(u"元") : Number(reiwa)) + u"年" + month_day);
        }
        forms.push_back(std::u16string(weekday) + u"曜日");
        break;
    }
    return forms;
}

bool IsDateReading(std::u16string_view reading)
{
    if (IsTimeReading(reading)) {
        return true;
    }
    for (const DateWord& word : kDateWords) {
        if (reading == word.reading) {
            return true;
        }
    }
    return false;
}

} // namespace astelio
