using System.IO;
using Microsoft.Win32;

namespace Astelio.Settings;

/// <summary>
/// The settings the TIP reads from HKCU\Software\AstelioIME (platform/windows/tip/src/learning_store.cpp). Every app
/// picks up a change at its next input or when it gets the focus.
/// </summary>
internal sealed class SettingsStore
{
    public const string DefaultKey = @"Software\AstelioIME";

    private readonly string _key;

    public SettingsStore(string key = DefaultKey)
    {
        _key = key;
    }

    // C-14, R-10, R-11, B-14, D-04 (on unless set to 0) and D-06 secret mode (off unless set to 1).
    public bool SharedInputMode { get => ReadFlag("SharedInputMode", true); set => WriteFlag("SharedInputMode", value); }
    public bool ListNumberPeriod { get => ReadFlag("ListNumberPeriod", true); set => WriteFlag("ListNumberPeriod", value); }
    public bool AutoCloseBrackets { get => ReadFlag("AutoCloseBrackets", true); set => WriteFlag("AutoCloseBrackets", value); }
    public bool TypoSuggestions { get => ReadFlag("TypoSuggestions", true); set => WriteFlag("TypoSuggestions", value); }
    public bool LearningEnabled { get => ReadFlag("LearningEnabled", true); set => WriteFlag("LearningEnabled", value); }
    public bool LearningPaused { get => ReadFlag("LearningPaused", false); set => WriteFlag("LearningPaused", value); }

    // C-13: one of DateFormats.Names; unknown values read as the first.
    public string DateFormat
    {
        get
        {
            using RegistryKey? key = Registry.CurrentUser.OpenSubKey(_key);
            return key?.GetValue("DateFormat") is string name && DateFormats.Names.Contains(name) ? name : DateFormats.Names[0];
        }
        set
        {
            if (!DateFormats.Names.Contains(value))
            {
                throw new ArgumentException($"Unknown date format: {value}", nameof(value));
            }
            using RegistryKey key = Registry.CurrentUser.CreateSubKey(_key);
            key.SetValue("DateFormat", value, RegistryValueKind.String);
        }
    }

    // C-09 / D-06: exe file names in lower case.
    public IReadOnlyList<string> DisabledApps => ReadApps("DisabledApps");
    public IReadOnlyList<string> NoLearningApps => ReadApps("NoLearningApps");
    public void SetAppDisabled(string app, bool listed) => SetAppListed("DisabledApps", app, listed);
    public void SetAppLearningExcluded(string app, bool listed) => SetAppListed("NoLearningApps", app, listed);

    // "Back to the defaults" of a page: the values are removed, so the TIP uses its own defaults.
    public void Reset(params string[] names)
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(_key, writable: true);
        foreach (string name in names)
        {
            key?.DeleteValue(name, throwOnMissingValue: false);
        }
    }

    /// <summary>An exe file name as the TIP compares it; null when it is not one.</summary>
    public static string? NormalizeApp(string text)
    {
        string name = Path.GetFileName(text.Trim().Trim('"')).ToLowerInvariant();
        if (name.Length == 0 || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            return null;
        }
        return name.EndsWith(".exe", StringComparison.Ordinal) ? name : name + ".exe";
    }

    private bool ReadFlag(string name, bool fallback)
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(_key);
        return key?.GetValue(name) is int value ? value != 0 : fallback;
    }

    private void WriteFlag(string name, bool on)
    {
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(_key);
        key.SetValue(name, on ? 1 : 0, RegistryValueKind.DWord);
    }

    private IReadOnlyList<string> ReadApps(string name)
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(_key);
        return key?.GetValue(name) is string[] apps ? apps.Where(app => app.Length > 0).ToArray() : [];
    }

    private void SetAppListed(string name, string app, bool listed)
    {
        string? normalized = NormalizeApp(app) ?? throw new ArgumentException($"Not an app name: {app}", nameof(app));
        List<string> apps = ReadApps(name).Where(existing => existing != normalized).ToList();
        if (listed)
        {
            apps.Add(normalized);
            apps.Sort(StringComparer.Ordinal);
        }
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(_key);
        if (apps.Count == 0)
        {
            key.DeleteValue(name, throwOnMissingValue: false);
        }
        else
        {
            key.SetValue(name, apps.ToArray(), RegistryValueKind.MultiString);
        }
    }
}
