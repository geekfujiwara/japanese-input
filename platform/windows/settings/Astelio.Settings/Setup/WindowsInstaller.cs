using System.IO;
using System.Runtime.InteropServices;

namespace Astelio.Settings.Setup;

/// <summary>The end of an install, from the MsiInstallProduct result.</summary>
internal enum InstallOutcome
{
    Installed,
    InstalledNeedsRestart,
    Canceled,
    Failed,
}

internal interface IInstaller
{
    /// <summary>Progress (0 to 1) and what is being done; called on any thread.</summary>
    event Action<double, string>? Progress;

    InstallOutcome Install(IntPtr owner, out uint code);

    void Cancel();
}

/// <summary>
/// Runs the MSI with the setup's own progress (MsiSetExternalUI). Only the UAC prompt of Windows Installer is shown,
/// so the setup itself does not need to run as administrator and writes the first settings for the user who runs
/// it. Files in use (every app that has the TIP loaded) are replaced at the next restart instead of closing apps.
/// </summary>
internal sealed class WindowsInstaller(string msiPath, string logPath) : IInstaller
{
    private const int InstallUiLevelNone = 2;
    private const int InstallUiLevelUacOnly = 0x200;
    private const uint FilterMessages = 0x1 | 0x2 | 0x4 | 0x8 | 0x20 | 0x100 | 0x200 | 0x400; // errors, files in use, progress
    private const uint LogMessages = 0x1FDF; // the "*v" log of msiexec
    private const uint LogFlushEachLine = 2;
    private const int IdCancel = 2;
    private const int IdIgnore = 5;

    private readonly MsiProgress _progress = new();
    private InstallUiHandler? _handler;
    private IntPtr _owner;
    private string _status = MsiProgress.Describe("");
    private volatile bool _cancel;

    public event Action<double, string>? Progress;

    public InstallOutcome Install(IntPtr owner, out uint code)
    {
        _owner = owner;
        _handler = OnMessage;
        MsiEnableLog(LogMessages, logPath, LogFlushEachLine);
        int previousLevel = MsiSetInternalUI(InstallUiLevelNone | InstallUiLevelUacOnly, owner);
        MsiSetExternalUI(_handler, FilterMessages, IntPtr.Zero);
        try
        {
            // The TIP is loaded in every app, so a restart is left to the user; so is closing apps.
            code = MsiInstallProduct(msiPath, "REBOOT=ReallySuppress MSIRESTARTMANAGERCONTROL=Disable");
        }
        finally
        {
            MsiSetExternalUI(null, 0, IntPtr.Zero);
            MsiSetInternalUI(previousLevel, IntPtr.Zero);
            GC.KeepAlive(_handler);
        }
        return code switch
        {
            0 => InstallOutcome.Installed,
            3010 or 1641 => InstallOutcome.InstalledNeedsRestart,
            1602 => InstallOutcome.Canceled,
            _ => InstallOutcome.Failed,
        };
    }

    public void Cancel() => _cancel = true;

    private int OnMessage(IntPtr context, uint type, string? message)
    {
        uint kind = type & 0xFF000000;
        switch (kind)
        {
            case 0x0A000000: // PROGRESS
                _progress.OnProgress(message ?? "");
                break;
            case 0x09000000: // ACTIONDATA
                _progress.OnActionData();
                break;
            case 0x08000000: // ACTIONSTART
                _status = MsiProgress.Describe(message ?? "");
                break;
            case 0x05000000: // FILESINUSE
                return IdIgnore;
            case 0x00000000: // FATALEXIT
            case 0x01000000: // ERROR
            case 0x02000000: // WARNING
            case 0x03000000: // USER
                return string.IsNullOrEmpty(message)
                    ? 0
                    : MessageBoxW(_owner, message, "Astelio IME のセットアップ", type & 0x00FFFFFF);
            default:
                return 0;
        }
        Progress?.Invoke(_progress.Fraction, _status);
        return _cancel ? IdCancel : 1;
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate int InstallUiHandler(IntPtr context, uint messageType, string? message);

    [DllImport("msi.dll", CharSet = CharSet.Unicode, EntryPoint = "MsiInstallProductW")]
    private static extern uint MsiInstallProduct(string packagePath, string commandLine);

    [DllImport("msi.dll")]
    private static extern int MsiSetInternalUI(int uiLevel, IntPtr window);

    [DllImport("msi.dll", CharSet = CharSet.Unicode, EntryPoint = "MsiSetExternalUIW")]
    private static extern IntPtr MsiSetExternalUI(InstallUiHandler? handler, uint messageFilter, IntPtr context);

    [DllImport("msi.dll", CharSet = CharSet.Unicode, EntryPoint = "MsiEnableLogW")]
    private static extern uint MsiEnableLog(uint logMode, string logFile, uint logAttributes);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(IntPtr window, string text, string caption, uint type);
}

/// <summary>--setup-preview: the wizard without installing anything, to look at it during development.</summary>
internal sealed class PreviewInstaller : IInstaller
{
    private volatile bool _cancel;

    public event Action<double, string>? Progress;

    public InstallOutcome Install(IntPtr owner, out uint code)
    {
        string[] steps = ["ファイルをコピーしています", "入力方式を登録しています", "キーボードの一覧に追加しています", "仕上げています"];
        for (int i = 0; i <= 100 && !_cancel; i++)
        {
            Progress?.Invoke(i / 100.0, steps[Math.Min(i / 26, steps.Length - 1)]);
            Thread.Sleep(80);
        }
        code = _cancel ? 1602u : 0u;
        return _cancel ? InstallOutcome.Canceled : InstallOutcome.Installed;
    }

    public void Cancel() => _cancel = true;
}

internal static class SetupFiles
{
    public static string LogPath => Path.Combine(Path.GetTempPath(), "AstelioIME-setup.log");

    public static string InstalledSettingsApp =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Astelio IME", "settings",
            "AstelioSettings.exe");
}
