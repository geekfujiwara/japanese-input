using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using Astelio.Settings.Tour;

namespace Astelio.Settings.Setup;

/// <summary>
/// The setup wizard (R-06〜R-08, U-03): welcome, what Astelio IME does, ready, install (the tour keeps playing),
/// first settings, done. `payload` is null for --setup-preview, which installs nothing.
/// </summary>
public partial class SetupWindow : Window
{
    private enum Step { Welcome, Tour, Ready, Installing, FirstSettings, Done }

    private readonly SetupPayload? _payload;
    private readonly IReadOnlyList<Demo> _demos = Demos.All(DateTime.Today);
    private readonly SettingsStore _store = new();
    private Step _step;
    private int _demo;
    private IInstaller? _installer;
    private bool _installed;

    internal SetupWindow(SetupPayload? payload)
    {
        InitializeComponent();
        _payload = payload;
        InstallContents.Text =
            $"入力方式（{RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant()} と 32ビットのアプリ用）、辞書、設定アプリ、使い方。" +
            "場所: " + System.IO.Path.GetDirectoryName(System.IO.Path.GetDirectoryName(SetupFiles.InstalledSettingsApp));
        FirstDateFormat.ItemsSource = DateFormats.Names
            .Select(name => new ComboBoxItem { Content = DateFormats.Example(name, DateTime.Today), Tag = name })
            .ToArray();
        foreach (Demo _ in _demos)
        {
            DemoDots.Children.Add(new Ellipse { Width = 8, Height = 8, Margin = new Thickness(0, 0, 6, 0) });
        }
        Loaded += (_, _) =>
        {
            if (SystemParameters.ClientAreaAnimation)
            {
                ((Storyboard)FindResource("Float")).Begin(this, true);
            }
        };
        Closing += OnClosing;
        Go(RuntimeInformation.OSArchitecture == RuntimeInformation.ProcessArchitecture ? Step.Welcome : Step.Done);
        if (_step == Step.Done)
        {
            ShowResult("この PC には別の版を使ってください",
                $"この PC は {RuntimeInformation.OSArchitecture.ToString().ToLowerInvariant()} です。" +
                "Releases から同じ種類の「AstelioIME-…-setup.exe」をダウンロードしてください。", restart: false, log: false);
        }
    }

    private void Go(Step step)
    {
        _step = step;
        WelcomePage.Visibility = Show(step == Step.Welcome);
        TourPage.Visibility = Show(step is Step.Tour or Step.Installing);
        ReadyPage.Visibility = Show(step == Step.Ready);
        FirstSettingsPage.Visibility = Show(step == Step.FirstSettings);
        DonePage.Visibility = Show(step == Step.Done);

        BackButton.Visibility = Show(step is Step.Tour or Step.Ready);
        CancelButton.Visibility = Show(step is Step.Welcome or Step.Tour or Step.Ready or Step.Installing);
        CancelButton.IsEnabled = true;
        NextButton.IsEnabled = step != Step.Installing;
        NextButton.Content = step switch
        {
            Step.Ready => "インストール",
            Step.Installing => "インストール中…",
            Step.Done => "閉じる",
            _ => "次へ",
        };
        StepText.Text = step switch
        {
            Step.Welcome => "1 / 5",
            Step.Tour => "2 / 5",
            Step.Ready => "3 / 5",
            Step.Installing => "4 / 5",
            Step.FirstSettings => "5 / 5",
            _ => "",
        };
        InstallProgress.Visibility = InstallStatus.Visibility = Show(step == Step.Installing);
        TourHeading.Text = step == Step.Installing ? "インストールしています" : "Astelio IME でできること";
        if (step is Step.Tour or Step.Installing)
        {
            PlayDemo(_demo);
        }
        else
        {
            Stage.Stop();
        }
        if (step == Step.FirstSettings)
        {
            LoadFirstSettings();
        }
    }

    private static Visibility Show(bool visible) => visible ? Visibility.Visible : Visibility.Collapsed;

    private void PlayDemo(int index)
    {
        _demo = (index + _demos.Count) % _demos.Count;
        Demo demo = _demos[_demo];
        DemoNumber.Text = $"{_demo + 1} / {_demos.Count}";
        DemoTitle.Text = demo.Title;
        DemoDescription.Text = demo.Description;
        for (int i = 0; i < DemoDots.Children.Count; i++)
        {
            var dot = (Ellipse)DemoDots.Children[i];
            if (i == _demo)
            {
                dot.Fill = new SolidColorBrush(Color.FromRgb(0x6B, 0x3F, 0xE6));
            }
            else
            {
                dot.SetResourceReference(Shape.FillProperty, "ControlStrokeColorDefaultBrush");
            }
        }
        Stage.Play(demo);
    }

    private void OnDemoFinished(object? sender, EventArgs e) => PlayDemo(_demo + 1);

    private void OnPreviousDemo(object sender, RoutedEventArgs e) => PlayDemo(_demo - 1);

    private void OnNextDemo(object sender, RoutedEventArgs e) => PlayDemo(_demo + 1);

    private void OnBack(object sender, RoutedEventArgs e) => Go(_step - 1);

    private void OnNext(object sender, RoutedEventArgs e)
    {
        switch (_step)
        {
            case Step.Welcome:
                Go(Step.Tour);
                break;
            case Step.Tour:
                Go(Step.Ready);
                break;
            case Step.Ready:
                Install();
                break;
            case Step.FirstSettings:
                SaveFirstSettings();
                Go(Step.Done);
                ShowResult("Astelio IME の準備ができました",
                    "右 Alt を1回押すと日本語、左 Alt を1回押すと英語です。\nすでに起動していたアプリは、起動し直すと使えます。\n" +
                    "一覧に出ないときは Win+Space で「Astelio IME」を選んでください。",
                    restart: _restartNeeded, log: false);
                break;
            case Step.Done:
                Close();
                break;
        }
    }

    private bool _restartNeeded;

    private async void Install()
    {
        Go(Step.Installing);
        IntPtr owner = new WindowInteropHelper(this).Handle;
        string? work = null;
        InstallOutcome outcome;
        uint code;
        try
        {
            (outcome, code) = await Task.Run(() =>
            {
                IInstaller installer;
                var held = new List<FileStream>();
                try
                {
                    if (_payload is null)
                    {
                        installer = new PreviewInstaller();
                    }
                    else
                    {
                        work = Directory.CreateTempSubdirectory("AstelioIME-setup-").FullName;
                        string msi;
                        using (FileStream self = File.OpenRead(Environment.ProcessPath!))
                        {
                            msi = _payload.Extract(self, work);
                        }
                        // Nothing may change the files between here and Windows Installer reading them.
                        held.Add(new FileStream(msi, FileMode.Open, FileAccess.Read, FileShare.Read));
                        held.Add(new FileStream(System.IO.Path.Combine(work, _payload.AppPath), FileMode.Open, FileAccess.Read, FileShare.Read));
                        installer = new WindowsInstaller(msi, SetupFiles.LogPath);
                    }
                    installer.Progress += (fraction, status) => Dispatcher.BeginInvoke(() =>
                    {
                        InstallProgress.Value = fraction;
                        InstallStatus.Text = status;
                    });
                    _installer = installer;
                    InstallOutcome result = installer.Install(owner, out uint resultCode);
                    return (result, resultCode);
                }
                finally
                {
                    held.ForEach(stream => stream.Dispose());
                }
            });
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            (outcome, code) = (InstallOutcome.Failed, 0u);
        }
        finally
        {
            _installer = null;
            if (work is not null)
            {
                try { Directory.Delete(work, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
        }

        switch (outcome)
        {
            case InstallOutcome.Installed:
            case InstallOutcome.InstalledNeedsRestart:
                _installed = true;
                _restartNeeded = outcome == InstallOutcome.InstalledNeedsRestart;
                Go(Step.FirstSettings);
                break;
            case InstallOutcome.Canceled:
                Go(Step.Done);
                ShowResult("インストールを取り消しました", "何も変更していません。もう一度インストールするときは、このファイルを開き直してください。",
                    restart: false, log: false);
                break;
            default:
                Go(Step.Done);
                ShowResult("インストールできませんでした", FailureText(code), restart: false, log: File.Exists(SetupFiles.LogPath));
                break;
        }
    }

    private static string FailureText(uint code) => code switch
    {
        1618 => "ほかのアプリのインストールが実行中です。終わってから、もう一度このファイルを開いてください。",
        1638 => "別の版の Astelio IME がすでに入っています。［設定］→［アプリ］から削除してから、もう一度お試しください。",
        1925 or 1303 => "管理者の権限が必要です。Windows の確認（ユーザーアカウント制御）で「はい」を選んでください。",
        0 => "インストールに使う一時ファイルを作れませんでした。ディスクの空きを確かめてください。",
        _ => $"Windows Installer がエラー {code} を返しました。お手数ですが、「ログを開く」で表示されるファイルを添えて GitHub の Issue でお知らせください（ログに入力した文字は含まれません）。",
    };

    private void ShowResult(string title, string text, bool restart, bool log)
    {
        DoneTitle.Text = title;
        DoneText.Text = text;
        RestartCard.Visibility = Show(restart);
        LogButton.Visibility = Show(log);
        DoneLinks.Children[0].Visibility = Show(_installed);
        DoneLinks.Visibility = Show(_installed || log);
    }

    private void LoadFirstSettings()
    {
        FirstLearning.IsChecked = _store.LearningEnabled;
        FirstTypo.IsChecked = _store.TypoSuggestions;
        FirstShared.IsChecked = _store.SharedInputMode;
        FirstListPeriod.IsChecked = _store.ListNumberPeriod;
        FirstBrackets.IsChecked = _store.AutoCloseBrackets;
        string format = _store.DateFormat;
        FirstDateFormat.SelectedItem = FirstDateFormat.Items.Cast<ComboBoxItem>().First(item => (string)item.Tag == format);
    }

    private void SaveFirstSettings()
    {
        _store.LearningEnabled = FirstLearning.IsChecked == true;
        _store.TypoSuggestions = FirstTypo.IsChecked == true;
        _store.SharedInputMode = FirstShared.IsChecked == true;
        _store.ListNumberPeriod = FirstListPeriod.IsChecked == true;
        _store.AutoCloseBrackets = FirstBrackets.IsChecked == true;
        if (FirstDateFormat.SelectedItem is ComboBoxItem item)
        {
            _store.DateFormat = (string)item.Tag;
        }
    }

    private void OnCancel(object sender, RoutedEventArgs e)
    {
        if (_step == Step.Installing)
        {
            CancelButton.IsEnabled = false;
            _installer?.Cancel();
            return;
        }
        Close();
    }

    private void OnClosing(object? sender, CancelEventArgs e)
    {
        // Windows Installer is rolled back through the cancel button, not by closing the window halfway.
        if (_step == Step.Installing)
        {
            e.Cancel = true;
            OnCancel(this, new RoutedEventArgs());
        }
    }

    private void OnRestart(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(this, "PC を再起動します。保存していない作業は保存してください。", Title,
                MessageBoxButton.OKCancel, MessageBoxImage.Information) == MessageBoxResult.OK)
        {
            using Process? _ = Process.Start(new ProcessStartInfo(System.IO.Path.Combine(Environment.SystemDirectory, "shutdown.exe"), "/r /t 0")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            });
        }
    }

    private void OnOpenSettings(object sender, RoutedEventArgs e)
    {
        if (File.Exists(SetupFiles.InstalledSettingsApp))
        {
            InstalledFiles.OpenWithShell(SetupFiles.InstalledSettingsApp);
        }
    }

    private void OnOpenLog(object sender, RoutedEventArgs e) => InstalledFiles.OpenWithShell(SetupFiles.LogPath);

    private void OnOpenLink(object sender, RoutedEventArgs e) => InstalledFiles.OpenWithShell((string)((Button)sender).Tag);

    private void OnShowLicense(object sender, RoutedEventArgs e)
    {
        var text = new TextBox
        {
            Text = ReadResource("LICENSE") + "\n\n" + ReadResource("THIRD_PARTY_NOTICES"),
            IsReadOnly = true,
            TextWrapping = TextWrapping.Wrap,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            FontFamily = new FontFamily("Consolas, Yu Gothic UI"),
            Margin = new Thickness(16),
        };
        new Window { Title = "ライセンス", Owner = this, Width = 720, Height = 560, Content = text, WindowStartupLocation = WindowStartupLocation.CenterOwner }
            .ShowDialog();
    }

    private static string ReadResource(string name)
    {
        using Stream? stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
        return stream is null ? "" : new StreamReader(stream).ReadToEnd();
    }
}
