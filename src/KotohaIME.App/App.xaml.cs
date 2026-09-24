using System.Windows;
using KotohaIME.Core;

namespace KotohaIME.App;

public partial class App : System.Windows.Application
{
	private KeyboardInterceptor? _keyboardInterceptor;
	private TrayIconService? _trayIcon;

	protected override void OnStartup(StartupEventArgs e)
	{
		base.OnStartup(e);

		var settingsStore = new SettingsStore();
		ImeSettings settings = settingsStore.Load();
		if (!settings.IsSetupCompleted)
		{
			var setupWizard = new SetupWizard(settings, settingsStore);
			MainWindow = setupWizard;
			if (setupWizard.ShowDialog() != true)
			{
				Shutdown();
				return;
			}
		}

		_keyboardInterceptor = new KeyboardInterceptor(() => settings, new ImeController());

		string? startupError = null;
		try
		{
			_keyboardInterceptor.Start();
			StartupRegistration.Apply(settings.StartWithWindows);
		}
		catch (Exception exception)
		{
			startupError = exception.Message;
		}

		var window = new MainWindow(settings, settingsStore, _keyboardInterceptor.IsRunning, startupError);
		MainWindow = window;
		_trayIcon = new TrayIconService(window, ShutdownApplication);
		window.Show();
	}

	private void ShutdownApplication()
	{
		if (MainWindow is MainWindow window)
		{
			window.AllowClose();
		}

		Shutdown();
	}

	protected override void OnExit(ExitEventArgs e)
	{
		_trayIcon?.Dispose();
		_keyboardInterceptor?.Dispose();
		base.OnExit(e);
	}
}

