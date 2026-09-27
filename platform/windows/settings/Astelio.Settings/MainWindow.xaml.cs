using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

namespace Astelio.Settings;

public partial class MainWindow : Window
{
    private sealed record Page(string Name, string Glyph, string Title, FrameworkElement Panel);

    private readonly SettingsStore _store = new();
    private readonly string? _app;
    private readonly Page[] _pages;
    private bool _loading;

    internal MainWindow(CommandLine args)
    {
        InitializeComponent();
        _app = args.App;
        _pages =
        [
            new("general", "\uE713", "全般", GeneralPage),
            new("typing", "\uE765", "入力補助", TypingPage),
            new("conversion", "\uE8C1", "変換", ConversionPage),
            new("history", "\uE81C", "入力の履歴", HistoryPage),
            new("dictionary", "\uE82D", "ユーザー辞書", DictionaryPage),
            new("apps", "\uE71D", "アプリごとの設定", AppsPage),
            new("guide", "\uE897", "使い方", GuidePage),
            new("about", "\uE946", "このアプリについて", AboutPage),
        ];
        Navigation.ItemsSource = _pages;

        DateTime today = DateTime.Today;
        DateFormat.ItemsSource = DateFormats.Names
            .Select(name => new ComboBoxItem { Content = DateFormats.Example(name, today), Tag = name })
            .ToArray();

        string version = Assembly.GetExecutingAssembly()
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion.Split('+')[0] ?? "";
        VersionText.Text = $"バージョン {version}";
        CopyrightText.Text = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyCopyrightAttribute>()?.Copyright;

        Load();
        Page first = _pages.FirstOrDefault(page => page.Name == args.Page) ?? _pages[0];
        Navigation.SelectedItem = first;
        Activated += (_, _) => Load();
    }

    // The TIP and other windows can change the same values, so they are read again whenever this window comes back.
    private void Load()
    {
        _loading = true;
        SharedInputMode.IsChecked = _store.SharedInputMode;
        ListNumberPeriod.IsChecked = _store.ListNumberPeriod;
        AutoCloseBrackets.IsChecked = _store.AutoCloseBrackets;
        TypoSuggestions.IsChecked = _store.TypoSuggestions;
        LearningEnabled.IsChecked = _store.LearningEnabled;
        LearningPaused.IsChecked = _store.LearningPaused;
        LearningPaused.IsEnabled = _store.LearningEnabled;
        string format = _store.DateFormat;
        DateFormat.SelectedItem = DateFormat.Items.Cast<ComboBoxItem>().First(item => (string)item.Tag == format);

        CurrentAppCard.Visibility = _app is null ? Visibility.Collapsed : Visibility.Visible;
        if (_app is not null)
        {
            CurrentAppTitle.Text = $"このアプリ（{_app}）";
            CurrentAppDisabled.IsChecked = _store.DisabledApps.Contains(_app);
            CurrentAppNoLearning.IsChecked = _store.NoLearningApps.Contains(_app);
        }
        DisabledApps.ItemsSource = _store.DisabledApps;
        NoLearningApps.ItemsSource = _store.NoLearningApps;
        _loading = false;
    }

    private void OnNavigate(object sender, SelectionChangedEventArgs e)
    {
        if (Navigation.SelectedItem is not Page selected)
        {
            return;
        }
        foreach (Page page in _pages)
        {
            page.Panel.Visibility = page == selected ? Visibility.Visible : Visibility.Collapsed;
        }
        PageScroller.ScrollToTop();
    }

    private void OnFlag(object sender, RoutedEventArgs e)
    {
        if (_loading || sender is not CheckBox box)
        {
            return;
        }
        bool on = box.IsChecked == true;
        switch ((string)box.Tag)
        {
            case "SharedInputMode": _store.SharedInputMode = on; break;
            case "ListNumberPeriod": _store.ListNumberPeriod = on; break;
            case "AutoCloseBrackets": _store.AutoCloseBrackets = on; break;
            case "TypoSuggestions": _store.TypoSuggestions = on; break;
            case "LearningEnabled": _store.LearningEnabled = on; break;
            case "LearningPaused": _store.LearningPaused = on; break;
        }
        Load();
    }

    private void OnDateFormat(object sender, SelectionChangedEventArgs e)
    {
        if (!_loading && DateFormat.SelectedItem is ComboBoxItem item)
        {
            _store.DateFormat = (string)item.Tag;
        }
    }

    private void OnReset(object sender, RoutedEventArgs e)
    {
        _store.Reset(((string)((Button)sender).Tag).Split(' '));
        Load();
    }

    private void OnCurrentAppDisabled(object sender, RoutedEventArgs e)
    {
        _store.SetAppDisabled(_app!, CurrentAppDisabled.IsChecked == true);
        Load();
    }

    private void OnCurrentAppNoLearning(object sender, RoutedEventArgs e)
    {
        _store.SetAppLearningExcluded(_app!, CurrentAppNoLearning.IsChecked == true);
        Load();
    }

    private void OnAddApp(object sender, RoutedEventArgs e)
    {
        bool disabled = (string)((Button)sender).Tag == "DisabledApps";
        ComboBox input = disabled ? DisabledAppInput : NoLearningAppInput;
        string? app = SettingsStore.NormalizeApp(input.Text);
        if (app is null)
        {
            MessageBox.Show(this, "アプリの実行ファイル名（例: notepad.exe）を入力してください。", Title,
                MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        if (disabled)
        {
            _store.SetAppDisabled(app, true);
        }
        else
        {
            _store.SetAppLearningExcluded(app, true);
        }
        input.Text = "";
        Load();
    }

    private void OnRemoveApp(object sender, RoutedEventArgs e)
    {
        bool disabled = (string)((Button)sender).Tag == "DisabledApps";
        if ((disabled ? DisabledApps : NoLearningApps).SelectedItem is not string app)
        {
            return;
        }
        if (disabled)
        {
            _store.SetAppDisabled(app, false);
        }
        else
        {
            _store.SetAppLearningExcluded(app, false);
        }
        Load();
    }

    // The apps with a window now, so the exe name does not have to be typed.
    private void OnListRunningApps(object? sender, EventArgs e)
    {
        var names = new SortedSet<string>(StringComparer.Ordinal);
        foreach (Process process in Process.GetProcesses())
        {
            using (process)
            {
                if (process.MainWindowHandle != IntPtr.Zero && SettingsStore.NormalizeApp(process.ProcessName) is string name)
                {
                    names.Add(name);
                }
            }
        }
        ((ComboBox)sender!).ItemsSource = names;
    }

    private void OnOpenLearningHistory(object sender, RoutedEventArgs e) => OpenTipWindow("AstelioTipOpenLearningHistory");

    private void OnOpenUserDictionary(object sender, RoutedEventArgs e) => OpenTipWindow("AstelioTipOpenUserDictionary");

    private void OpenTipWindow(string entryPoint)
    {
        if (!InstalledFiles.OpenTipWindow(entryPoint))
        {
            ShowMissing();
        }
    }

    private void OnOpenGuide(object sender, RoutedEventArgs e) => OpenFile(InstalledFiles.GuidePath);

    private void OnOpenNotices(object sender, RoutedEventArgs e) => OpenFile(InstalledFiles.NoticesPath);

    private void OnOpenLink(object sender, RoutedEventArgs e) => InstalledFiles.OpenWithShell((string)((Button)sender).Tag);

    private void OpenFile(string path)
    {
        if (File.Exists(path))
        {
            InstalledFiles.OpenWithShell(path);
        }
        else
        {
            ShowMissing();
        }
    }

    private void ShowMissing() =>
        MessageBox.Show(this, "Astelio IME のファイルが見つかりません。Astelio IME をインストールし直してください。", Title,
            MessageBoxButton.OK, MessageBoxImage.Warning);
}
