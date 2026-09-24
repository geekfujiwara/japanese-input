using System.Windows;
using KotohaIME.Core;

namespace KotohaIME.App;

public partial class SetupWizard : Window
{
    private const int LastStep = 2;
    private readonly ImeSettings _settings;
    private readonly SettingsStore _settingsStore;
    private int _currentStep;

    public SetupWizard(ImeSettings settings, SettingsStore settingsStore)
    {
        InitializeComponent();
        _settings = settings;
        _settingsStore = settingsStore;

        WizardAsciiCheckBox.IsChecked = settings.ForceHalfWidthAscii;
        WizardPunctuationCheckBox.IsChecked = settings.ForceHalfWidthJapanesePunctuation;
        WizardLeftAltCheckBox.IsChecked = settings.LeftAltSwitchesToEnglish;
        WizardRightAltCheckBox.IsChecked = settings.RightAltSwitchesToJapanese;
        WizardStartupCheckBox.IsChecked = settings.StartWithWindows;
    }

    private void NextButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentStep < LastStep)
        {
            _currentStep++;
            UpdateStep();
            return;
        }

        CompleteSetup();
    }

    private void BackButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentStep > 0)
        {
            _currentStep--;
            UpdateStep();
        }
    }

    private void CancelButton_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
    }

    private void CompleteSetup()
    {
        _settings.ForceHalfWidthAscii = WizardAsciiCheckBox.IsChecked == true;
        _settings.ForceHalfWidthJapanesePunctuation = WizardPunctuationCheckBox.IsChecked == true;
        _settings.LeftAltSwitchesToEnglish = WizardLeftAltCheckBox.IsChecked == true;
        _settings.RightAltSwitchesToJapanese = WizardRightAltCheckBox.IsChecked == true;
        _settings.StartWithWindows = WizardStartupCheckBox.IsChecked == true;
        _settings.IsSetupCompleted = true;

        try
        {
            _settingsStore.Save(_settings);
            DialogResult = true;
        }
        catch (Exception exception)
        {
            _settings.IsSetupCompleted = false;
            WizardErrorText.Text = exception.Message;
            WizardErrorPanel.Visibility = Visibility.Visible;
        }
    }

    private void UpdateStep()
    {
        WelcomePage.Visibility = _currentStep == 0 ? Visibility.Visible : Visibility.Collapsed;
        InputPage.Visibility = _currentStep == 1 ? Visibility.Visible : Visibility.Collapsed;
        StartupPage.Visibility = _currentStep == 2 ? Visibility.Visible : Visibility.Collapsed;
        BackButton.Visibility = _currentStep == 0 ? Visibility.Collapsed : Visibility.Visible;
        NextButton.Content = _currentStep == LastStep ? "設定を完了" : "次へ";

        UpdateMarker(StepOneMarker, StepOneCircle, _currentStep == 0);
        UpdateMarker(StepTwoMarker, StepTwoCircle, _currentStep == 1);
        UpdateMarker(StepThreeMarker, StepThreeCircle, _currentStep == 2);
    }

    private static void UpdateMarker(FrameworkElement marker, System.Windows.Controls.Border circle, bool active)
    {
        marker.Opacity = active ? 1 : 0.55;
        circle.Background = active
            ? System.Windows.Media.Brushes.White
            : System.Windows.Media.Brushes.Transparent;
    }
}