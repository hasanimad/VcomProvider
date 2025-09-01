using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace VCom.Client.Models;

public class ConfigService
{
    private readonly string _configFilePath;
    private readonly JsonSerializerOptions _jsonOptions;

    public ConfigService()
    {
        var appDataPath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var configFolder = Path.Combine(appDataPath, "VComClient");
        Directory.CreateDirectory(configFolder);
        _configFilePath = Path.Combine(configFolder, "config.json");

        _jsonOptions = new JsonSerializerOptions
        {
            WriteIndented = true,
            Converters = { new JsonStringEnumConverter() }
        };
    }

    public AppConfig LoadConfig()
    {
        if (!File.Exists(_configFilePath))
        {
            return new AppConfig();
        }
        try
        {
            var json = File.ReadAllText(_configFilePath);
            return JsonSerializer.Deserialize<AppConfig>(json, _jsonOptions) ?? new AppConfig();
        }
        catch { return new AppConfig(); }
    }

    public void SaveConfig(AppConfig config)
    {
        try
        {
            var json = JsonSerializer.Serialize(config, _jsonOptions);
            File.WriteAllText(_configFilePath, json);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Error saving config: {ex.Message}");
        }
    }
}