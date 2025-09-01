using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace VCom.Core.Models
{
    public class AppConfig
    {
        public List<PortConfig> Ports { get; set; } = new List<PortConfig>();
    }

    /// <summary>
    /// Represents the configuration for a single virtual COM port.
    /// Inherits from ObservableObject to support live UI updates.
    /// </summary>
    public class PortConfig : ObservableObject
    {
        private bool _isEnabled;
        private ForwardingStatus _status = ForwardingStatus.Idle;

        public string ComPortName { get; set; } = string.Empty;
        public string DevicePath { get; set; } = string.Empty;

        /// <summary>
        /// Gets or sets whether this port's forwarding is active.
        /// When this value is changed, it notifies the UI to update the toggle switch.
        /// </summary>
        public bool IsEnabled
        {
            get => _isEnabled;
            set
            {
                if (_isEnabled != value)
                {
                    _isEnabled = value;
                    OnPropertyChanged();
                }
            }
        }

        public NetworkSettings Network { get; set; } = new NetworkSettings();
        public SerialSettings Serial { get; set; } = new SerialSettings();

        /// <summary>
        /// The current runtime status of the forwarding service.
        /// This is not saved to the config file. When this value is changed,
        /// it notifies the UI to update the status indicator color.
        /// </summary>
        [JsonIgnore]
        public ForwardingStatus Status
        {
            get => _status;
            set
            {
                if (_status != value)
                {
                    _status = value;
                    OnPropertyChanged();
                }
            }
        }
    }

    // --- Supporting classes and enums (unchanged) ---
    public class NetworkSettings
    {
        public string Hostname { get; set; } = "127.0.0.1";
        public int Port { get; set; } = 4000;
        public ProtocolType Protocol { get; set; } = ProtocolType.Tcp;
    }

    public class SerialSettings
    {
        public int BaudRate { get; set; } = 9600;
        public int DataBits { get; set; } = 8;
        public Parity Parity { get; set; } = Parity.None;
        public StopBits StopBits { get; set; } = StopBits.One;
    }

    public enum ProtocolType { Tcp, Udp }
    public enum Parity { None, Odd, Even, Mark, Space }
    public enum StopBits { One, OnePointFive, Two }
}
