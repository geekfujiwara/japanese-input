using System.Globalization;

namespace Astelio.Settings;

/// <summary>C-13: the date forms of B-09, in the order and with the names of kDateFormatNames (core).</summary>
internal static class DateFormats
{
    public static readonly IReadOnlyList<string> Names =
        ["yyyy/MM/dd", "yyyy年M月d日", "M月d日", "M月d日(ddd)", "令和", "yyyy-MM-dd", "yyyyMMdd", "M/d"];

    /// <summary>How `name` writes `date`, as the IME does.</summary>
    public static string Example(string name, DateTime date)
    {
        CultureInfo japanese = CultureInfo.GetCultureInfo("ja-JP");
        return name switch
        {
            "M月d日(ddd)" => date.ToString("M月d日", japanese) + "(" + japanese.DateTimeFormat.GetShortestDayName(date.DayOfWeek) + ")",
            "令和" => Reiwa(date),
            _ => date.ToString(name, CultureInfo.InvariantCulture),
        };
    }

    private static string Reiwa(DateTime date)
    {
        int year = date.Year - 2018;
        string era = year == 1 ? "元" : year.ToString(CultureInfo.InvariantCulture);
        return date < new DateTime(2019, 5, 1) ? "" : $"令和{era}年{date.Month}月{date.Day}日";
    }
}
