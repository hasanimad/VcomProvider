using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using VCom.Core.Models;

namespace VCom.Core
{
    /// <summary>
    /// Manages the TCP or UDP network connection for data forwarding.
    /// This class abstracts the specific protocol details away from the main forwarder logic.
    /// </summary>
    public class NetworkClient : IDisposable
    {
        private readonly NetworkSettings _config;
        private TcpClient? _tcpClient;
        private UdpClient? _udpClient;
        private NetworkStream? _stream;

        /// <summary>
        /// Gets a value indicating whether the network client is connected.
        /// </summary>
        public bool IsConnected => _tcpClient?.Connected ?? (_udpClient != null);

        public NetworkClient(NetworkSettings config)
        {
            _config = config;
        }

        /// <summary>
        /// Establishes a connection to the configured network endpoint.
        /// </summary>
        /// <param name="cancellationToken">A token to cancel the connection attempt.</param>
        public async Task ConnectAsync(CancellationToken cancellationToken)
        {
            if (_config.Protocol == Models.ProtocolType.Tcp)
            {
                _tcpClient = new TcpClient();
                // Use the CancellationToken-aware overload for graceful cancellation.
                await _tcpClient.ConnectAsync(_config.Hostname, _config.Port, cancellationToken);
                _stream = _tcpClient.GetStream();
            }
            else // UDP
            {
                _udpClient = new UdpClient();
                // UDP is "connectionless," but Connect() establishes a default remote host
                // so we don't have to specify it on every SendAsync call.
                _udpClient.Connect(_config.Hostname, _config.Port);
            }
        }

        /// <summary>
        /// Receives data from the network.
        /// </summary>
        /// <param name="buffer">The buffer to store the received data.</param>
        /// <param name="cancellationToken">A token to cancel the receive operation.</param>
        /// <returns>The number of bytes received.</returns>
        public async Task<int> ReceiveAsync(byte[] buffer, CancellationToken cancellationToken)
        {
            if (!IsConnected)
                throw new InvalidOperationException("Network client is not connected.");

            if (_config.Protocol == Models.ProtocolType.Tcp)
            {
                if (_stream == null) return 0;
                return await _stream.ReadAsync(buffer, cancellationToken);
            }
            else // UDP
            {
                var result = await _udpClient!.ReceiveAsync(cancellationToken);
                Array.Copy(result.Buffer, buffer, result.Buffer.Length);
                return result.Buffer.Length;
            }
        }

        /// <summary>
        /// Sends data over the network.
        /// </summary>
        /// <param name="data">The buffer containing the data to send.</param>
        /// <param name="count">The number of bytes to send from the buffer.</param>
        /// <param name="cancellationToken">A token to cancel the send operation.</param>
        public async Task SendAsync(byte[] data, int count, CancellationToken cancellationToken)
        {
            if (!IsConnected)
                throw new InvalidOperationException("Network client is not connected.");
            if (count == 0) return;

            if (_config.Protocol == Models.ProtocolType.Tcp)
            {
                if (_stream == null) return;
                await _stream.WriteAsync(data.AsMemory(0, count), cancellationToken);
            }
            else // UDP
            {
                await _udpClient!.SendAsync(data.AsMemory(0, count));
            }
        }

        /// <summary>
        /// Closes the connection and disposes of all network resources.
        /// </summary>
        public void Dispose()
        {
            _stream?.Dispose();
            _tcpClient?.Dispose();
            _udpClient?.Dispose();
        }
    }
}
