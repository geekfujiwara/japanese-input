namespace Astelio.Settings.Tests;

public sealed class DateFormatsTests
{
    // The same names, in the same order, as kDateFormatNames in core/src/special_candidates.cpp.
    [Fact]
    public void NamesMatchTheCore()
    {
        Assert.Equal(
            ["yyyy/MM/dd", "yyyy年M月d日", "M月d日", "M月d日(ddd)", "令和", "yyyy-MM-dd", "yyyyMMdd", "M/d"],
            DateFormats.Names);
    }

    [Theory]
    [InlineData("yyyy/MM/dd", "2026/09/07")]
    [InlineData("yyyy年M月d日", "2026年9月7日")]
    [InlineData("M月d日", "9月7日")]
    [InlineData("M月d日(ddd)", "9月7日(月)")]
    [InlineData("令和", "令和8年9月7日")]
    [InlineData("yyyy-MM-dd", "2026-09-07")]
    [InlineData("yyyyMMdd", "20260907")]
    [InlineData("M/d", "9/7")]
    public void ExamplesAreWrittenAsTheImeWritesThem(string name, string expected)
    {
        Assert.Equal(expected, DateFormats.Example(name, new DateTime(2026, 9, 7)));
    }

    [Fact]
    public void FirstYearOfReiwaIsGannen()
    {
        Assert.Equal("令和元年5月1日", DateFormats.Example("令和", new DateTime(2019, 5, 1)));
    }
}
