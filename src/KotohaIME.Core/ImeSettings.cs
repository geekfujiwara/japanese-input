namespace KotohaIME.Core;

public sealed class ImeSettings
{
    public bool IsSetupCompleted { get; set; }

    public bool IsEnabled { get; set; } = true;

    public bool ForceHalfWidthAscii { get; set; } = true;

    public bool ForceHalfWidthJapanesePunctuation { get; set; }

    public bool LeftAltSwitchesToEnglish { get; set; } = true;

    public bool RightAltSwitchesToJapanese { get; set; } = true;

    public bool StartWithWindows { get; set; }
}