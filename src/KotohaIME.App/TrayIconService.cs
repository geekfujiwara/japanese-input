using System.Drawing;
using System.IO;
using Forms = System.Windows.Forms;

namespace KotohaIME.App;

public sealed class TrayIconService : IDisposable
{
    private readonly MainWindow _window;
    private readonly Icon _icon;
    private readonly Forms.NotifyIcon _notifyIcon;

    public TrayIconService(MainWindow window, Action exitApplication)
    {
        _window = window;

        using Stream iconStream = System.Windows.Application.GetResourceStream(
            new Uri("pack://application:,,,/Assets/AstelioIME.ico"))?.Stream
            ?? throw new InvalidOperationException("Astelio IMEのアイコンを読み込めませんでした。");
        using var loadedIcon = new Icon(iconStream);
        _icon = (Icon)loadedIcon.Clone();

        var contextMenu = new Forms.ContextMenuStrip();
        contextMenu.Items.Add("設定を開く", null, (_, _) => ShowSettings());
        contextMenu.Items.Add(new Forms.ToolStripSeparator());
        contextMenu.Items.Add("Astelio IMEを終了", null, (_, _) => exitApplication());

        _notifyIcon = new Forms.NotifyIcon
        {
            Text = "Astelio IME",
            Icon = _icon,
            ContextMenuStrip = contextMenu,
            Visible = true,
        };
        _notifyIcon.DoubleClick += (_, _) => ShowSettings();
    }

    public void Dispose()
    {
        _notifyIcon.Visible = false;
        _notifyIcon.ContextMenuStrip?.Dispose();
        _notifyIcon.Dispose();
        _icon.Dispose();
    }

    private void ShowSettings()
    {
        if (_window.Dispatcher.CheckAccess())
        {
            _window.ShowFromTray();
        }
        else
        {
            _window.Dispatcher.Invoke(_window.ShowFromTray);
        }
    }
}