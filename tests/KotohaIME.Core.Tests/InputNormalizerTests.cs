using KotohaIME.Core;

namespace KotohaIME.Core.Tests;

public class InputNormalizerTests
{
    [Fact]
    public void DefaultSettings_RequireInitialSetup()
    {
        Assert.False(new ImeSettings().IsSetupCompleted);
    }

    [Theory]
    [InlineData("a")]
    [InlineData("Z")]
    [InlineData(" ")]
    public void LettersAndSpace_PreserveImeComposition(string input)
    {
        Assert.True(InputNormalizer.ShouldPreserveImeComposition(input));
    }

    [Theory]
    [InlineData("1")]
    [InlineData(".")]
    [InlineData("あ")]
    public void NonAsciiLetters_DoNotUseCompositionException(string input)
    {
        Assert.False(InputNormalizer.ShouldPreserveImeComposition(input));
    }

    [Theory]
    [InlineData("a", true)]
    [InlineData("Z", true)]
    [InlineData("1", true)]
    [InlineData("+", true)]
    [InlineData("(", true)]
    [InlineData(" ", false)]
    [InlineData("。", false)]
    public void HalfWidthImeComposition_UsesConfiguredAsciiTargets(string input, bool expected)
    {
        Assert.Equal(expected, InputNormalizer.ShouldUseHalfWidthImeComposition(input, new ImeSettings()));
    }

    [Fact]
    public void HalfWidthImeComposition_DisabledSettingDoesNotApply()
    {
        var settings = new ImeSettings { ForceHalfWidthAscii = false };

        Assert.False(InputNormalizer.ShouldUseHalfWidthImeComposition("a", settings));
    }

    [Theory]
    [InlineData("１", "1")]
    [InlineData("Ａ", "A")]
    [InlineData("　", " ")]
    [InlineData("ー", "-")]
    [InlineData("￥", "\\")]
    [InlineData("｛", "{")]
    public void DefaultSettings_NormalizeConfiguredAsciiCharacters(string input, string expected)
    {
        bool changed = InputNormalizer.TryNormalize(input, new ImeSettings(), out string actual);

        Assert.True(changed);
        Assert.Equal(expected, actual);
    }

    [Theory]
    [InlineData("「")]
    [InlineData("。")]
    [InlineData("？")]
    public void DefaultSettings_LeaveJapanesePunctuationAlone(string input)
    {
        bool changed = InputNormalizer.TryNormalize(input, new ImeSettings(), out string actual);

        Assert.False(changed);
        Assert.Equal(input, actual);
    }

    [Theory]
    [InlineData("[", "｢")]
    [InlineData("]", "｣")]
    [InlineData("/", "･")]
    [InlineData(".", "｡")]
    public void JapanesePunctuationOption_UsesHalfWidthForms(string input, string expected)
    {
        var settings = new ImeSettings { ForceHalfWidthJapanesePunctuation = true };

        bool changed = InputNormalizer.TryNormalize(input, settings, out string actual);

        Assert.True(changed);
        Assert.Equal(expected, actual);
    }

    [Fact]
    public void Disabled_DoesNotNormalize()
    {
        var settings = new ImeSettings { IsEnabled = false };

        bool changed = InputNormalizer.TryNormalize("１", settings, out string actual);

        Assert.False(changed);
        Assert.Equal("１", actual);
    }
}