namespace Astelio.Settings;

/// <summary>
/// Arguments from the TIP: --app &lt;exe&gt; (the app whose "settings" menu item was used, so its own switches are at
/// hand) and --page &lt;name&gt;.
/// </summary>
internal sealed record CommandLine(string? App, string? Page)
{
    public static CommandLine Parse(IReadOnlyList<string> args)
    {
        string? app = null;
        string? page = null;
        for (int i = 0; i + 1 < args.Count; i += 2)
        {
            switch (args[i])
            {
                case "--app":
                    app = SettingsStore.NormalizeApp(args[i + 1]);
                    break;
                case "--page":
                    page = args[i + 1];
                    break;
            }
        }
        return new CommandLine(app, page);
    }
}
