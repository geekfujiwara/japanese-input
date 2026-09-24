using KotohaIME.Core;

namespace KotohaIME.Core.Tests;

public class AltPressTrackerTests
{
    [Theory]
    [InlineData(AltKeySide.Left)]
    [InlineData(AltKeySide.Right)]
    public void Release_AfterPress_IsEmptyPress(AltKeySide side)
    {
        var tracker = new AltPressTracker();

        tracker.Press(side);

        Assert.True(tracker.Release(side));
    }

    [Theory]
    [InlineData(AltKeySide.Left)]
    [InlineData(AltKeySide.Right)]
    public void Release_AfterChord_IsNotEmptyPress(AltKeySide side)
    {
        var tracker = new AltPressTracker();

        tracker.Press(side);
        tracker.MarkChordUsed();

        Assert.False(tracker.Release(side));
    }

    [Fact]
    public void RepeatedKeyDown_DoesNotResetChordState()
    {
        var tracker = new AltPressTracker();
        tracker.Press(AltKeySide.Left);
        tracker.MarkChordUsed();

        tracker.Press(AltKeySide.Left);

        Assert.False(tracker.Release(AltKeySide.Left));
    }

    [Fact]
    public void BothAltKeys_AreNotEmptyPresses()
    {
        var tracker = new AltPressTracker();

        tracker.Press(AltKeySide.Left);
        tracker.Press(AltKeySide.Right);

        Assert.False(tracker.Release(AltKeySide.Right));
        Assert.False(tracker.Release(AltKeySide.Left));
    }
}