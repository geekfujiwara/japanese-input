using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using KotohaIME.Core;

namespace KotohaIME.App;

public sealed class KeyboardInterceptor : IDisposable
{
    private const int WhKeyboardLowLevel = 13;
    private const uint WmKeyDown = 0x0100;
    private const uint WmKeyUp = 0x0101;
    private const uint WmSystemKeyDown = 0x0104;
    private const uint WmSystemKeyUp = 0x0105;
    private const uint LlkhfExtended = 0x01;
    private const uint LlkhfInjected = 0x10;
    private const uint VkMenu = 0x12;
    private const uint VkLeftMenu = 0xA4;
    private const uint VkRightMenu = 0xA5;
    private const uint VkControl = 0x11;
    private const uint VkLWin = 0x5B;
    private const uint VkRWin = 0x5C;
    private const uint KeyEventUnicode = 0x0004;
    private const uint KeyEventKeyUp = 0x0002;
    private const uint KeyEventExtendedKey = 0x0001;
    private const uint InputKeyboard = 1;
    private const nuint AstelioInjectedInput = 0x41535445;

    private readonly Func<ImeSettings> _getSettings;
    private readonly IImeController _imeController;
    private readonly HookProcedure _hookProcedure;
    private readonly HashSet<uint> _suppressedKeys = [];
    private readonly AltPressTracker _altPressTracker = new();
    private readonly HashSet<AltKeySide> _suppressedAltKeys = [];
    private readonly HashSet<AltKeySide> _replayedAltKeys = [];
    private nint _hook;

    public KeyboardInterceptor(Func<ImeSettings> getSettings, IImeController imeController)
    {
        _getSettings = getSettings;
        _imeController = imeController;
        _hookProcedure = HandleKeyboardMessage;
    }

    public bool IsRunning => _hook != 0;

    internal int SuppressedAltEventCount { get; private set; }

    public void Start()
    {
        if (_hook != 0)
        {
            return;
        }

        using Process process = Process.GetCurrentProcess();
        using ProcessModule module = process.MainModule
            ?? throw new InvalidOperationException("アプリケーションモジュールを取得できませんでした。");
        nint moduleHandle = GetModuleHandle(module.ModuleName);
        _hook = SetWindowsHookEx(WhKeyboardLowLevel, _hookProcedure, moduleHandle, 0);

        if (_hook == 0)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "キーボードフックを開始できませんでした。");
        }
    }

    public void Dispose()
    {
        foreach (AltKeySide side in _replayedAltKeys)
        {
            SendAlt(side, keyUp: true);
        }
        _replayedAltKeys.Clear();
        _suppressedAltKeys.Clear();

        if (_hook != 0)
        {
            UnhookWindowsHookEx(_hook);
            _hook = 0;
        }
        GC.SuppressFinalize(this);
    }

    private nint HandleKeyboardMessage(int code, nint message, nint data)
    {
        if (code < 0)
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        var keyboardData = Marshal.PtrToStructure<KeyboardHookData>(data);
        if ((keyboardData.Flags & LlkhfInjected) != 0 &&
            keyboardData.ExtraInfo == AstelioInjectedInput)
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        uint messageCode = unchecked((uint)message.ToInt64());
        bool isKeyDown = messageCode is WmKeyDown or WmSystemKeyDown;
        bool isKeyUp = messageCode is WmKeyUp or WmSystemKeyUp;
        ImeSettings settings = _getSettings();

        if (IsAltKey(keyboardData))
        {
            AltKeySide side = (keyboardData.Flags & LlkhfExtended) != 0
                ? AltKeySide.Right
                : AltKeySide.Left;
            bool handlesSide = settings.IsEnabled &&
                (side == AltKeySide.Left
                    ? settings.LeftAltSwitchesToEnglish
                    : settings.RightAltSwitchesToJapanese);

            if (isKeyDown)
            {
                if (!handlesSide && !_suppressedAltKeys.Contains(side))
                {
                    return CallNextHookEx(_hook, code, message, data);
                }

                _altPressTracker.Press(side);
                _suppressedAltKeys.Add(side);
                SuppressedAltEventCount++;
                return 1;
            }

            if (isKeyUp && _suppressedAltKeys.Remove(side))
            {
                bool wasEmptyPress = _altPressTracker.Release(side);
                if (_replayedAltKeys.Remove(side))
                {
                    SendAlt(side, keyUp: true);
                }
                else if (wasEmptyPress && handlesSide && side == AltKeySide.Right)
                {
                    _imeController.SwitchToJapanese();
                }
                else if (wasEmptyPress && handlesSide)
                {
                    _imeController.SwitchToEnglish();
                }

                SuppressedAltEventCount++;
                return 1;
            }

            return CallNextHookEx(_hook, code, message, data);
        }

        if (isKeyDown)
        {
            _altPressTracker.MarkChordUsed();
            ReplaySuppressedAltKeys();
        }

        if (isKeyUp && _suppressedKeys.Remove(keyboardData.VirtualKeyCode))
        {
            return 1;
        }

        if (!isKeyDown || !settings.IsEnabled || HasCommandModifier())
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        string? typedCharacter = TranslateKey(keyboardData.VirtualKeyCode, keyboardData.ScanCode);
        if (typedCharacter is not null &&
            InputNormalizer.ShouldUseHalfWidthImeComposition(typedCharacter, settings))
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        if (typedCharacter is null || InputNormalizer.ShouldPreserveImeComposition(typedCharacter))
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        if (
            !InputNormalizer.TryNormalize(typedCharacter, settings, out string replacement) ||
            !SendUnicode(replacement))
        {
            return CallNextHookEx(_hook, code, message, data);
        }

        _suppressedKeys.Add(keyboardData.VirtualKeyCode);
        return 1;
    }

    internal nint ProcessKeyboardMessageForTest(uint message, uint virtualKey, uint scanCode, uint flags)
    {
        var keyboardData = new KeyboardHookData
        {
            VirtualKeyCode = virtualKey,
            ScanCode = scanCode,
            Flags = flags,
        };
        nint data = Marshal.AllocHGlobal(Marshal.SizeOf<KeyboardHookData>());
        try
        {
            Marshal.StructureToPtr(keyboardData, data, false);
            return HandleKeyboardMessage(0, (nint)message, data);
        }
        finally
        {
            Marshal.FreeHGlobal(data);
        }
    }

    private static bool IsAltKey(KeyboardHookData data) =>
        data.VirtualKeyCode == VkMenu || data.ScanCode == 0x38;

    private static bool HasCommandModifier() =>
        IsPressed(VkControl) || IsPressed(VkLWin) || IsPressed(VkRWin) || IsPressed(VkMenu);

    private static bool IsPressed(uint virtualKey) => (GetAsyncKeyState((int)virtualKey) & 0x8000) != 0;

    private void ReplaySuppressedAltKeys()
    {
        foreach (AltKeySide side in _suppressedAltKeys)
        {
            if (_replayedAltKeys.Add(side))
            {
                SendAlt(side, keyUp: false);
            }
        }
    }

    private static bool SendAlt(AltKeySide side, bool keyUp)
    {
        Input input = Input.ForVirtualKey(
            side == AltKeySide.Left ? VkLeftMenu : VkRightMenu,
            extended: side == AltKeySide.Right,
            keyUp);
        return SendInput(1, [input], Marshal.SizeOf<Input>()) == 1;
    }

    private static string? TranslateKey(uint virtualKey, uint scanCode)
    {
        var keyboardState = new byte[256];
        if (!GetKeyboardState(keyboardState))
        {
            return null;
        }

        keyboardState[virtualKey] |= 0x80;
        uint threadId = GetWindowThreadProcessId(GetForegroundWindow(), out _);
        nint keyboardLayout = GetKeyboardLayout(threadId);
        var buffer = new StringBuilder(8);
        int length = ToUnicodeEx(virtualKey, scanCode, keyboardState, buffer, buffer.Capacity, 0, keyboardLayout);
        return length == 1 ? buffer.ToString(0, 1) : null;
    }

    private static bool SendUnicode(string text)
    {
        var inputs = new Input[text.Length * 2];
        for (int index = 0; index < text.Length; index++)
        {
            inputs[index * 2] = Input.ForUnicode(text[index], keyUp: false);
            inputs[(index * 2) + 1] = Input.ForUnicode(text[index], keyUp: true);
        }

        return SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>()) == inputs.Length;
    }

    private delegate nint HookProcedure(int code, nint message, nint data);

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardHookData
    {
        public uint VirtualKeyCode;
        public uint ScanCode;
        public uint Flags;
        public uint Time;
        public nuint ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint Type;
        public InputUnion Data;

        public static Input ForUnicode(char character, bool keyUp) => new()
        {
            Type = InputKeyboard,
            Data = new InputUnion
            {
                Keyboard = new KeyboardInput
                {
                    ScanCode = character,
                    Flags = KeyEventUnicode | (keyUp ? KeyEventKeyUp : 0),
                    ExtraInfo = AstelioInjectedInput,
                },
            },
        };

        public static Input ForVirtualKey(uint virtualKey, bool extended, bool keyUp) => new()
        {
            Type = InputKeyboard,
            Data = new InputUnion
            {
                Keyboard = new KeyboardInput
                {
                    VirtualKeyCode = (ushort)virtualKey,
                    Flags = (extended ? KeyEventExtendedKey : 0) | (keyUp ? KeyEventKeyUp : 0),
                    ExtraInfo = AstelioInjectedInput,
                },
            },
        };
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public KeyboardInput Keyboard;
        [FieldOffset(0)] public MouseInput Mouse;
        [FieldOffset(0)] public HardwareInput Hardware;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort VirtualKeyCode;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public nuint ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int X;
        public int Y;
        public uint MouseData;
        public uint Flags;
        public uint Time;
        public nuint ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HardwareInput
    {
        public uint Message;
        public ushort ParameterLow;
        public ushort ParameterHigh;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SetWindowsHookEx(int hookId, HookProcedure procedure, nint module, uint threadId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(nint hook);

    [DllImport("user32.dll")]
    private static extern nint CallNextHookEx(nint hook, int code, nint message, nint data);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern nint GetModuleHandle(string? moduleName);

    [DllImport("user32.dll")]
    private static extern short GetAsyncKeyState(int virtualKey);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetKeyboardState(byte[] keyboardState);

    [DllImport("user32.dll")]
    private static extern nint GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint processId);

    [DllImport("user32.dll")]
    private static extern nint GetKeyboardLayout(uint threadId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int ToUnicodeEx(
        uint virtualKey,
        uint scanCode,
        byte[] keyboardState,
        [Out] StringBuilder buffer,
        int bufferSize,
        uint flags,
        nint keyboardLayout);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint inputCount, Input[] inputs, int inputSize);
}