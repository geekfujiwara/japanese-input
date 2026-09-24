using System.IO;
using System.Text.Json;
using KotohaIME.Core;

namespace KotohaIME.App;

public sealed class SettingsStore
{
    private static readonly JsonSerializerOptions SerializerOptions = new() { WriteIndented = true };
    private readonly string _settingsPath;
    private readonly string _legacySettingsPath;

    public SettingsStore()
    {
        string localApplicationData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        string directory = Path.Combine(localApplicationData, "AstelioIME");
        _settingsPath = Path.Combine(directory, "settings.json");
        _legacySettingsPath = Path.Combine(localApplicationData, "KotohaIME", "settings.json");
    }

    public ImeSettings Load()
    {
        try
        {
            string? sourcePath = File.Exists(_settingsPath)
                ? _settingsPath
                : File.Exists(_legacySettingsPath) ? _legacySettingsPath : null;
            return sourcePath is not null
                ? JsonSerializer.Deserialize<ImeSettings>(File.ReadAllText(sourcePath)) ?? new ImeSettings()
                : new ImeSettings();
        }
        catch (JsonException)
        {
            return new ImeSettings();
        }
    }

    public void Save(ImeSettings settings)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
        File.WriteAllText(_settingsPath, JsonSerializer.Serialize(settings, SerializerOptions));
    }
}