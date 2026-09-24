using System.Windows;
using System.ComponentModel;
using KotohaIME.Core;

namespace KotohaIME.App;

public partial class MainWindow : Window
{
    private readonly ImeSettings _settings;
    private readonly SettingsStore _settingsStore;
    private bool _isLoading = true;
    private bool _allowClose;

    public MainWindow(ImeSettings settings, SettingsStore settingsStore, bool hookIsRunning, string? startupError)
    {
        InitializeComponent();
        _settings = settings;
        _settingsStore = settingsStore;

        EnabledCheckBox.IsChecked = settings.IsEnabled;
        AsciiCheckBox.IsChecked = settings.ForceHalfWidthAscii;
        JapanesePunctuationCheckBox.IsChecked = settings.ForceHalfWidthJapanesePunctuation;
        LeftAltCheckBox.IsChecked = settings.LeftAltSwitchesToEnglish;
        RightAltCheckBox.IsChecked = settings.RightAltSwitchesToJapanese;
        StartupCheckBox.IsChecked = settings.StartWithWindows;
        _isLoading = false;

        if (!hookIsRunning)
        {
            StatusText.Text = "停止中";
            StatusBadge.Background = new System.Windows.Media.SolidColorBrush(
                System.Windows.Media.Color.FromRgb(255, 225, 211));
        }

        if (!string.IsNullOrWhiteSpace(startupError))
        {
            ShowError(startupError);
        }
    }

    private void SettingChanged(object sender, RoutedEventArgs e)
    {
        if (_isLoading)
        {
            return;
        }

        _settings.IsEnabled = EnabledCheckBox.IsChecked == true;
        _settings.ForceHalfWidthAscii = AsciiCheckBox.IsChecked == true;
        _settings.ForceHalfWidthJapanesePunctuation = JapanesePunctuationCheckBox.IsChecked == true;
        _settings.LeftAltSwitchesToEnglish = LeftAltCheckBox.IsChecked == true;
        _settings.RightAltSwitchesToJapanese = RightAltCheckBox.IsChecked == true;
        _settings.StartWithWindows = StartupCheckBox.IsChecked == true;

        try
        {
            _settingsStore.Save(_settings);
            StartupRegistration.Apply(_settings.StartWithWindows);
            SaveStatusText.Text = $"保存済み  {DateTime.Now:HH:mm:ss}";
            ErrorPanel.Visibility = Visibility.Collapsed;
        }
        catch (Exception exception)
        {
            ShowError(exception.Message);
        }
    }

    private void ShowError(string message)
    {
        ErrorText.Text = message;
        ErrorPanel.Visibility = Visibility.Visible;
    }

    public void ShowFromTray()
    {
        Show();
        WindowState = WindowState.Normal;
        Activate();
        Topmost = true;
        Topmost = false;
        Focus();
    }

    public void AllowClose() => _allowClose = true;

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_allowClose)
        {
            e.Cancel = true;
            Hide();
        }

        base.OnClosing(e);
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e) => Hide();
}