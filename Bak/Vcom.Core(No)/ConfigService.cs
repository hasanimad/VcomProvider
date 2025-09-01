using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using VCom.Core.Models;

namespace VCom.Core
{
    public class ConfigService
    {
        private readonly string _configFilePath;
        private readonly JsonSerializerOptions _jsonOptions;

        public ConfigService()
        {
            // Store the config file in the user's local app data folder.
            // This is the standard location for application settings.
            string appDataPath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string configFolder = Path.Combine(appDataPath, "VComClient");
            Directory.CreateDirectory(configFolder); // Ensure the folder exists
            _configFilePath = Path.Combine(configFolder, "config.json");

            // Configure the JSON serializer for pretty printing.
            _jsonOptions = new JsonSerializerOptions
            {
                WriteIndented = true,
                // This will allow our enums to be saved as strings (e.g., "Tcp") instead of numbers.
                Converters = { new JsonStringEnumConverter() }
            };
        }

        /// <summary>
        /// Loads the application configuration from the config.json file.
        /// If the file doesn't exist, it returns a new, empty configuration.
        /// </summary>
        public AppConfig LoadConfig()
        {
            if (!File.Exists(_configFilePath))
            {
                return new AppConfig(); // Return a default config
            }

            try
            {
                string json = File.ReadAllText(_configFilePath);
                var config = JsonSerializer.Deserialize<AppConfig>(json, _jsonOptions);
                return config ?? new AppConfig();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error loading config: {ex.Message}");
                // If the file is corrupt, return a default config to prevent a crash.
                return new AppConfig();
            }
        }

        /// <summary>
        /// Saves the provided application configuration to the config.json file.
        /// </summary>
        public void SaveConfig(AppConfig config)
        {
            try
            {
                string json = JsonSerializer.Serialize(config, _jsonOptions);
                File.WriteAllText(_configFilePath, json);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error saving config: {ex.Message}");
            }
        }
    }
}
