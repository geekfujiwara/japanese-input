using System.Runtime.InteropServices;

namespace KotohaIME.App;

public interface IImeController
{
    void SwitchToEnglish();

    void SwitchToJapanese();
}

public sealed class ImeController : IImeController
{
    private const uint WmImeControl = 0x0283;
    private const nuint ImcSetOpenStatus = 0x0006;
    private const uint SmtoAbortIfHung = 0x0002;

    public void SwitchToEnglish() => SetOpenStatus(false);

    public void SwitchToJapanese() => SetOpenStatus(true);

    private static void SetOpenStatus(bool isOpen)
    {
        nint window = GetFocusedWindow();
        if (window == 0)
        {
            return;
        }

        nint imeWindow = ImmGetDefaultIMEWnd(window);
        if (imeWindow != 0 && TrySendImeControl(imeWindow, ImcSetOpenStatus, isOpen ? 1 : 0, out _))
        {
            return;
        }

        nint inputContext = ImmGetContext(window);
        if (inputContext == 0)
        {
            return;
        }

        try
        {
            ImmSetOpenStatus(inputContext, isOpen);
        }
        finally
        {
            ImmReleaseContext(window, inputContext);
        }
    }

    private static nint GetFocusedWindow()
    {
        nint foregroundWindow = GetForegroundWindow();
        uint threadId = GetWindowThreadProcessId(foregroundWindow, out _);
        var threadInfo = new GuiThreadInfo { Size = (uint)Marshal.SizeOf<GuiThreadInfo>() };
        return GetGUIThreadInfo(threadId, ref threadInfo) && threadInfo.FocusWindow != 0
            ? threadInfo.FocusWindow
            : foregroundWindow;
    }

    private static bool TrySendImeControl(nint imeWindow, nuint command, nint value, out nuint result) =>
        SendMessageTimeout(
            imeWindow,
            WmImeControl,
            command,
            value,
            SmtoAbortIfHung,
            200,
            out result) != 0;

    [StructLayout(LayoutKind.Sequential)]
    private struct GuiThreadInfo
    {
        public uint Size;
        public uint Flags;
        public nint ActiveWindow;
        public nint FocusWindow;
        public nint CaptureWindow;
        public nint MenuOwnerWindow;
        public nint MoveSizeWindow;
        public nint CaretWindow;
        public System.Drawing.Rectangle CaretRectangle;
    }

    [DllImport("user32.dll")]
    private static extern nint GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetGUIThreadInfo(uint threadId, ref GuiThreadInfo threadInfo);

    [DllImport("imm32.dll")]
    private static extern nint ImmGetDefaultIMEWnd(nint window);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SendMessageTimeout(
        nint window,
        uint message,
        nuint wParam,
        nint lParam,
        uint flags,
        uint timeout,
        out nuint result);

    [DllImport("imm32.dll")]
    private static extern nint ImmGetContext(nint window);

    [DllImport("imm32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ImmSetOpenStatus(nint inputContext, [MarshalAs(UnmanagedType.Bool)] bool isOpen);

    [DllImport("imm32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ImmReleaseContext(nint window, nint inputContext);
}