using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using Astelio.Settings.Setup;

namespace Astelio.Settings;

public partial class App : Application
{
    private Mutex? _single;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // The setup exe is this app with the MSI appended (Setup/SetupPayload.cs).
        SetupPayload? payload = ReadPayload();
        bool setup = payload is not null || e.Args.Contains("--setup-preview");
        _single = new Mutex(initiallyOwned: true, setup ? @"Local\AstelioIME.Setup" : @"Local\AstelioIME.Settings", out bool first);
        if (!first)
        {
            BringOtherToFront();
            Shutdown();
            return;
        }
        Window window = setup ? new SetupWindow(payload) : new MainWindow(CommandLine.Parse(e.Args));
        MainWindow = window;
        window.Show();
    }

    private static SetupPayload? ReadPayload()
    {
        try
        {
            using FileStream self = File.OpenRead(Environment.ProcessPath!);
            return SetupPayload.Read(self);
        }
        catch (IOException)
        {
            return null;
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _single?.Dispose();
        base.OnExit(e);
    }

    private static void BringOtherToFront()
    {
        using Process current = Process.GetCurrentProcess();
        foreach (Process other in Process.GetProcessesByName(current.ProcessName))
        {
            using (other)
            {
                if (other.Id != current.Id && other.MainWindowHandle != IntPtr.Zero)
                {
                    ShowWindow(other.MainWindowHandle, 9); // SW_RESTORE
                    SetForegroundWindow(other.MainWindowHandle);
                }
            }
        }
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindow(IntPtr window, int command);
}
