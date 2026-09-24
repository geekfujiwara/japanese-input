using KotohaIME.App;
using KotohaIME.Core;
using Xunit;

namespace KotohaIME.App.Tests;

public sealed class AltHookDecisionTests
{
    private const uint WmSystemKeyDown = 0x0104;
    private const uint WmSystemKeyUp = 0x0105;
    private const uint WmKeyDown = 0x0100;
    private const uint LlkhfExtended = 0x01;
    private const uint VkLeftMenu = 0xA4;
    private const uint VkRightMenu = 0xA5;
    private const uint AltScanCode = 0x38;
    private const uint VkA = 0x41;
    private const uint AScanCode = 0x1E;

    [Theory]
    [InlineData(VkLeftMenu, 0)]
    [InlineData(VkRightMenu, LlkhfExtended)]
    public void AltAlone_DownAndUpAreBothSuppressed(uint virtualKey, uint flags)
    {
        var settings = new ImeSettings
        {
            IsEnabled = true,
            LeftAltSwitchesToEnglish = true,
            RightAltSwitchesToJapanese = true,
        };
        using var interceptor = new KeyboardInterceptor(() => settings, new ImeController());

        nint downResult = interceptor.ProcessKeyboardMessageForTest(
            WmSystemKeyDown,
            virtualKey,
            AltScanCode,
            flags);
        nint upResult = interceptor.ProcessKeyboardMessageForTest(
            WmSystemKeyUp,
            virtualKey,
            AltScanCode,
            flags);

        Assert.Equal(1, downResult);
        Assert.Equal(1, upResult);
        Assert.Equal(2, interceptor.SuppressedAltEventCount);
    }

    [Fact]
    public void RightAltThenAscii_KeepsJapaneseModeSelected()
    {
        var settings = new ImeSettings
        {
            IsEnabled = true,
            ForceHalfWidthAscii = true,
            RightAltSwitchesToJapanese = true,
        };
        var controller = new RecordingImeController();
        using var interceptor = new KeyboardInterceptor(() => settings, controller);

        interceptor.ProcessKeyboardMessageForTest(
            WmSystemKeyDown,
            VkRightMenu,
            AltScanCode,
            LlkhfExtended);
        interceptor.ProcessKeyboardMessageForTest(
            WmSystemKeyUp,
            VkRightMenu,
            AltScanCode,
            LlkhfExtended);
        interceptor.ProcessKeyboardMessageForTest(WmKeyDown, VkA, AScanCode, 0);

        Assert.Equal(1, controller.JapaneseSwitchCount);
        Assert.Equal(0, controller.EnglishSwitchCount);
    }

    private sealed class RecordingImeController : IImeController
    {
        public int EnglishSwitchCount { get; private set; }

        public int JapaneseSwitchCount { get; private set; }

        public void SwitchToEnglish() => EnglishSwitchCount++;

        public void SwitchToJapanese() => JapaneseSwitchCount++;
    }
}