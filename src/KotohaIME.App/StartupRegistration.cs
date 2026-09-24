using Microsoft.Win32;

namespace KotohaIME.App;

public static class StartupRegistration
{
    private const string RegistryPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "AstelioIME";
    private const string LegacyValueName = "KotohaIME";

    public static void Apply(bool enabled)
    {
        using RegistryKey key = Registry.CurrentUser.CreateSubKey(RegistryPath);
        key.DeleteValue(LegacyValueName, throwOnMissingValue: false);
        if (enabled)
        {
            string executablePath = Environment.ProcessPath
                ?? throw new InvalidOperationException("実行ファイルのパスを取得できませんでした。");
            key.SetValue(ValueName, $"\"{executablePath}\"");
        }
        else
        {
            key.DeleteValue(ValueName, throwOnMissingValue: false);
        }
    }
}