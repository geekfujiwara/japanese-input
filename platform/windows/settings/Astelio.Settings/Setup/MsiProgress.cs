using System.Text.RegularExpressions;

namespace Astelio.Settings.Setup;

/// <summary>
/// Follows the progress messages of Windows Installer (INSTALLMESSAGE_PROGRESS / ACTIONDATA / ACTIONSTART) as the
/// "Parsing a progress message" sample of the Windows Installer documentation does.
/// </summary>
internal sealed partial class MsiProgress
{
    private long _total;
    private long _position;
    private long _step;
    private bool _forward = true;
    private bool _stepOnActionData;
    private bool _script;

    /// <summary>0 to 1 over the whole install; the script generation takes the first tenth.</summary>
    public double Fraction
    {
        get
        {
            double part = _total <= 0 ? 0 : Math.Clamp((double)_position / _total, 0, 1);
            return _script ? part * 0.1 : 0.1 + part * 0.9;
        }
    }

    public void OnProgress(string message)
    {
        var fields = new long[5];
        foreach (Match match in ProgressField().Matches(message))
        {
            int index = match.Groups[1].Value[0] - '0';
            if (index is >= 1 and <= 4 && long.TryParse(match.Groups[2].Value, out long value))
            {
                fields[index] = value;
            }
        }
        switch (fields[1])
        {
            case 0:
                _total = fields[2];
                _forward = fields[3] == 0;
                _position = _forward ? 0 : _total;
                _stepOnActionData = false;
                _script = fields[4] == 1;
                break;
            case 1:
                _stepOnActionData = fields[3] != 0;
                _step = fields[2];
                break;
            case 2:
                Move(fields[2]);
                break;
            case 3:
                _total += fields[2];
                break;
        }
    }

    public void OnActionData()
    {
        if (_stepOnActionData)
        {
            Move(_step);
        }
    }

    /// <summary>What to show for the action in an ACTIONSTART message ("Action 17:10:30: InstallFiles. ...").</summary>
    public static string Describe(string message)
    {
        Match match = ActionName().Match(message);
        return (match.Success ? match.Groups[1].Value : "") switch
        {
            "RemoveExistingProducts" => "前の版を削除しています",
            "InstallFiles" or "MoveFiles" or "DuplicateFiles" => "ファイルをコピーしています",
            "RegisterNative" or "RegisterX86" => "入力方式を登録しています",
            "CreateShortcuts" => "スタートメニューに追加しています",
            "InstallFinalize" => "仕上げています",
            "EnableForUser" => "キーボードの一覧に追加しています",
            _ => "インストールしています",
        };
    }

    private void Move(long amount) => _position += _forward ? amount : -amount;

    [GeneratedRegex(@"([1-4]):\s*(-?\d+)")]
    private static partial Regex ProgressField();

    [GeneratedRegex(@"\d{1,2}:\d{2}:\d{2}:\s*([A-Za-z_][A-Za-z0-9_]*)")]
    private static partial Regex ActionName();
}
