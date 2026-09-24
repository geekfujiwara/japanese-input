namespace KotohaIME.Core;

public enum AltKeySide
{
    Left,
    Right,
}

public sealed class AltPressTracker
{
    private bool _leftIsDown;
    private bool _rightIsDown;
    private bool _leftWasUsed;
    private bool _rightWasUsed;

    public void Press(AltKeySide side)
    {
        if (side == AltKeySide.Left && !_leftIsDown)
        {
            _leftIsDown = true;
            _leftWasUsed = _rightIsDown;
            _rightWasUsed |= _rightIsDown;
        }
        else if (side == AltKeySide.Right && !_rightIsDown)
        {
            _rightIsDown = true;
            _rightWasUsed = _leftIsDown;
            _leftWasUsed |= _leftIsDown;
        }
    }

    public void MarkChordUsed()
    {
        _leftWasUsed |= _leftIsDown;
        _rightWasUsed |= _rightIsDown;
    }

    public bool Release(AltKeySide side)
    {
        if (side == AltKeySide.Left)
        {
            bool wasEmptyPress = _leftIsDown && !_leftWasUsed;
            _leftIsDown = false;
            _leftWasUsed = false;
            return wasEmptyPress;
        }

        bool rightWasEmptyPress = _rightIsDown && !_rightWasUsed;
        _rightIsDown = false;
        _rightWasUsed = false;
        return rightWasEmptyPress;
    }
}