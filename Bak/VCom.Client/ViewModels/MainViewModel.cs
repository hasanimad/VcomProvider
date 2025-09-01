using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using VCom.Client.Commands;
using VCom.Client.Native; // Use our new Native wrapper
using VCom.Client.Models; // We still use the PortConfig model

namespace VCom.Client.ViewModels;

public class MainViewModel : ObservableObject, IDisposable
{
    private PortConfig? _selectedPort;
    private string _outgoingDataLog = string.Empty;
    private string _incomingDataLog = string.Empty;

    // Keep callback delegates as member variables to prevent garbage collection
    private readonly VComCoreApi.StatusCallback _statusCallbackDelegate;
    private readonly VComCoreApi.DataCallback _dataCallbackDelegate;

    public ObservableCollection<PortConfig> Ports
    {
        get;
    }
    public ICommand SaveCommand
    {
        get;
    }
    public ICommand RefreshCommand
    {
        get;
    }

    public MainViewModel()
    {
        Ports = new ObservableCollection<PortConfig>();

        // Create the delegates and store them
        _statusCallbackDelegate = OnStatusUpdate;
        _dataCallbackDelegate = OnDataReceived;

        // Initialize the C++ Core Library
        VComCoreApi.VCom_Initialize(_statusCallbackDelegate, _dataCallbackDelegate);

        RefreshCommand = new RelayCommand(param => RefreshPortsList());
        SaveCommand = new RelayCommand(SaveConfiguration);

        RefreshPortsList();
    }

    public void Dispose()
    {
        // Clean up the C++ core when the application closes
        VComCoreApi.VCom_Shutdown();
    }

    private void RefreshPortsList()
    {
        // Stop listening to old events
        foreach (var port in Ports) { port.PropertyChanged -= OnPortPropertyChanged; }
        Ports.Clear();

        // 1. Call the C++ function
        string jsonResult = VComCoreApi.VCom_GetDeviceList();

        // 2. Parse the JSON string
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        var devices = JsonSerializer.Deserialize<ObservableCollection<PortConfig>>(jsonResult, options);

        // 3. Load saved settings and merge them
        var savedConfig = new ConfigService().LoadConfig(); // We still use this for settings

        if (devices != null)
        {
            foreach (var device in devices)
            {
                var savedPort = savedConfig.Ports.FirstOrDefault(p => p.DevicePath == device.DevicePath);
                if (savedPort != null)
                {
                    device.Network = savedPort.Network;
                    device.Serial = savedPort.Serial;
                }
                device.PropertyChanged += OnPortPropertyChanged;
                Ports.Add(device);
            }
        }
    }

    // --- Callbacks from C++ ---

    private void OnStatusUpdate(string devicePath, Models.ForwardingStatus status, string message)
    {
        Application.Current.Dispatcher.Invoke(() =>
        {
            var port = Ports.FirstOrDefault(p => p.DevicePath == devicePath);
            if (port != null)
            {
                port.Status = (VCom.Client.Native.ForwardingStatus)status; // Cast the enum
            }
        });
    }

    private void OnDataReceived(string devicePath, IntPtr dataPtr, int dataLength, int direction)
    {
        Application.Current.Dispatcher.Invoke(() =>
        {
            if (SelectedPort?.DevicePath == devicePath)
            {
                byte[] data = new byte[dataLength];
                Marshal.Copy(dataPtr, data, 0, dataLength);
                string hexString = BitConverter.ToString(data).Replace("-", " ") + " ";

                if (direction == 0) // Outgoing
                    OutgoingDataLog += hexString;
                else // Incoming
                    IncomingDataLog += hexString;
            }
        });
    }

    // --- UI Logic ---

    private void OnPortPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(PortConfig.IsEnabled))
        {
            if (sender is not PortConfig port) return;

            if (port.IsEnabled)
            {
                VComCoreApi.VCom_StartForwarding(port.DevicePath, port.Network.Hostname, port.Network.Port, (ProtocolType)port.Network.Protocol);
            }
            else
            {
                VComCoreApi.VCom_StopForwarding(port.DevicePath);
            }
        }
    }

    private void SaveConfiguration(object? parameter)
    {
        var appConfig = new AppConfig { Ports = new System.Collections.Generic.List<PortConfig>(Ports) };
        new ConfigService().SaveConfig(appConfig);
        MessageBox.Show("Configuration saved successfully!", "Save", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    #region Properties
    public PortConfig? SelectedPort
    {
        get => _selectedPort;
        set
        {
            if (_selectedPort != value) { _selectedPort = value; OnPropertyChanged(); OutgoingDataLog = ""; IncomingDataLog = ""; }
        }
    }
    public string OutgoingDataLog
    {
        get => _outgoingDataLog;
        set
        {
            _outgoingDataLog = value; OnPropertyChanged();
        }
    }
    public string IncomingDataLog
    {
        get => _incomingDataLog;
        set
        {
            _incomingDataLog = value; OnPropertyChanged();
        }
    }
    #endregion
}