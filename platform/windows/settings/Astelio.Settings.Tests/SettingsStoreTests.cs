using Microsoft.Win32;

namespace Astelio.Settings.Tests;

// T-C12-1: the settings app writes the values the TIP reads (platform/windows/tip/src/learning_store.cpp).
public sealed class SettingsStoreTests : IDisposable
{
    private readonly string _key = $@"Software\AstelioIME\Tests\Settings-{Guid.NewGuid():N}";
    private readonly SettingsStore _store;

    public SettingsStoreTests()
    {
        _store = new SettingsStore(_key);
    }

    public void Dispose()
    {
        Registry.CurrentUser.DeleteSubKeyTree(_key, throwOnMissingSubKey: false);
    }

    [Fact]
    public void DefaultsMatchTheTip()
    {
        Assert.True(_store.SharedInputMode);
        Assert.True(_store.ListNumberPeriod);
        Assert.True(_store.AutoCloseBrackets);
        Assert.True(_store.TypoSuggestions);
        Assert.True(_store.LearningEnabled);
        Assert.False(_store.LearningPaused);
        Assert.Equal("yyyy/MM/dd", _store.DateFormat);
        Assert.Empty(_store.DisabledApps);
        Assert.Empty(_store.NoLearningApps);
    }

    [Fact]
    public void FlagsAreDwordsTheTipReads()
    {
        _store.AutoCloseBrackets = false;
        _store.LearningPaused = true;

        using RegistryKey key = Registry.CurrentUser.OpenSubKey(_key)!;
        Assert.Equal(RegistryValueKind.DWord, key.GetValueKind("AutoCloseBrackets"));
        Assert.Equal(0, key.GetValue("AutoCloseBrackets"));
        Assert.Equal(1, key.GetValue("LearningPaused"));
        Assert.False(_store.AutoCloseBrackets);
        Assert.True(_store.LearningPaused);
    }

    [Fact]
    public void DateFormatIsOneOfTheCoreNames()
    {
        _store.DateFormat = "令和";

        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(_key)!)
        {
            Assert.Equal(RegistryValueKind.String, key.GetValueKind("DateFormat"));
            Assert.Equal("令和", key.GetValue("DateFormat"));
        }
        Assert.Throws<ArgumentException>(() => _store.DateFormat = "dd.MM.yyyy");
        using (RegistryKey key = Registry.CurrentUser.CreateSubKey(_key))
        {
            key.SetValue("DateFormat", "unknown");
        }
        Assert.Equal("yyyy/MM/dd", _store.DateFormat);
    }

    [Fact]
    public void AppListsAreLowerCaseExeNames()
    {
        _store.SetAppDisabled("C:\\Games\\Game.EXE", true);
        _store.SetAppDisabled("mstsc", true);
        _store.SetAppDisabled("game.exe", true);
        _store.SetAppLearningExcluded("KeePass.exe", true);

        Assert.Equal(["game.exe", "mstsc.exe"], _store.DisabledApps);
        Assert.Equal(["keepass.exe"], _store.NoLearningApps);
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(_key)!)
        {
            Assert.Equal(RegistryValueKind.MultiString, key.GetValueKind("DisabledApps"));
        }

        _store.SetAppDisabled("game.exe", false);
        _store.SetAppDisabled("mstsc.exe", false);
        Assert.Empty(_store.DisabledApps);
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(_key)!)
        {
            Assert.Null(key.GetValue("DisabledApps"));
        }
    }

    [Theory]
    [InlineData("notepad.exe", "notepad.exe")]
    [InlineData("  \"C:\\Windows\\Notepad.exe\" ", "notepad.exe")]
    [InlineData("WINWORD", "winword.exe")]
    [InlineData("", null)]
    [InlineData("a|b", null)]
    public void NormalizesAppNames(string text, string? expected)
    {
        Assert.Equal(expected, SettingsStore.NormalizeApp(text));
    }

    [Fact]
    public void ResetRemovesTheValues()
    {
        _store.TypoSuggestions = false;
        _store.DateFormat = "M/d";

        _store.Reset("TypoSuggestions", "DateFormat");

        Assert.True(_store.TypoSuggestions);
        Assert.Equal("yyyy/MM/dd", _store.DateFormat);
    }
}
