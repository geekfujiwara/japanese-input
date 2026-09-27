using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace Astelio.Settings;

/// <summary>
/// The installed files next to this app: &lt;install&gt;\settings\AstelioSettings.exe, &lt;install&gt;\&lt;arch&gt;\astelio_tip.dll
/// and &lt;install&gt;\guide\index.html.
/// </summary>
internal static class InstalledFiles
{
    public static string InstallDirectory => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));

    public static string TipPath => Path.Combine(InstallDirectory, NativeArchitecture, "astelio_tip.dll");

    public static string GuidePath => Path.Combine(InstallDirectory, "guide", "index.html");

    public static string NoticesPath => Path.Combine(InstallDirectory, "THIRD_PARTY_NOTICES.txt");

    // The installer puts the settings app of the same architecture as the TIP folder next to it.
    private static string NativeArchitecture =>
        RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "arm64" : "x64";

    /// <summary>
    /// Opens a window of the TIP (the user dictionary or the input history) in its own process; it keeps running
    /// until the window closes. False when the TIP is not installed next to this app.
    /// </summary>
    public static bool OpenTipWindow(string entryPoint)
    {
        if (!File.Exists(TipPath))
        {
            return false;
        }
        var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "rundll32.exe"))
        {
            UseShellExecute = false,
        };
        start.ArgumentList.Add($"{TipPath},{entryPoint}");
        using Process? process = Process.Start(start);
        return process is not null;
    }

    /// <summary>Opens a file or a web page with the app the user chose for it.</summary>
    public static void OpenWithShell(string target)
    {
        using Process? process = Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
    }
}
