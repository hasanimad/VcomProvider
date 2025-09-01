using System;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using VCom.Core.Models;

namespace VCom.Core;

/// <summary>
/// Orchestrates the bidirectional data forwarding between a DriverClient and a NetworkClient.
/// </summary>
[System.Runtime.Versioning.SupportedOSPlatform("windows")]
public class DataForwarder
{
    private readonly DriverClient _driverClient;
    private readonly NetworkClient _networkClient;
    private CancellationTokenSource? _cancellationTokenSource;

    public PortConfig Config
    {
        get;
    }

    public event Action<ForwardingStatus, string>? StatusChanged;
    public event Action<byte[]>? DataReceivedFromDriver;
    public event Action<byte[]>? DataReceivedFromNetwork;

    public DataForwarder(PortConfig config)
    {
        Config = config;
        _driverClient = new DriverClient(config.DevicePath);
        _networkClient = new NetworkClient(config.Network);
    }

    public void Start()
    {
        if (_cancellationTokenSource != null && !_cancellationTokenSource.IsCancellationRequested)
        {
            Debug.WriteLine($"Forwarder for {Config.ComPortName} is already running.");
            return;
        }

        _cancellationTokenSource = new CancellationTokenSource();
        var token = _cancellationTokenSource.Token;

        UpdateStatus(ForwardingStatus.Running, "Starting...");

        Task.Run(async () =>
        {
            try
            {
                _driverClient.Open();
                await _networkClient.ConnectAsync(token);
                UpdateStatus(ForwardingStatus.Connected, "Connection established.");
                _driverClient.Start();

                var driverToNetworkTask = DriverToNetworkLoop(token);
                var networkToDriverTask = NetworkToDriverLoop(token);

                // Wait for either task to complete (or fail).
                await Task.WhenAny(driverToNetworkTask, networkToDriverTask);
            }
            catch (OperationCanceledException)
            {
                // This is a graceful stop, expected behavior.
            }
            catch (Exception ex)
            {
                // This catches errors during initial connection (e.g., host not found).
                Debug.WriteLine($"Error during forwarder startup for {Config.ComPortName}: {ex.Message}");
                UpdateStatus(ForwardingStatus.Error, ex.Message);
            }
            finally
            {
                // Ensure everything is cleaned up regardless of how the loop ended.
                _cancellationTokenSource?.Cancel(); // Signal the other loop to stop if it hasn't already.
                _driverClient.Dispose();
                _networkClient.Dispose();

                // Only set to Idle if we aren't already in an error state from startup.
                if (Config.Status != ForwardingStatus.Error)
                {
                    UpdateStatus(ForwardingStatus.Idle, "Stopped.");
                }
            }
        }, token);
    }

    public void Stop()
    {
        _cancellationTokenSource?.Cancel();
    }

    private async Task DriverToNetworkLoop(CancellationToken token)
    {
        var buffer = new byte[4096];
        try
        {
            while (!token.IsCancellationRequested)
            {
                var bytesRead = _driverClient.GetOutgoingData(buffer, token);
                if (bytesRead > 0)
                {
                    var data = buffer.AsSpan(0, bytesRead).ToArray();
                    DataReceivedFromDriver?.Invoke(data);
                    await _networkClient.SendAsync(data, data.Length, token);
                }
                else if (bytesRead == 0 && token.IsCancellationRequested)
                {
                    // Graceful exit when cancelled.
                    break;
                }
            }
        }
        catch (OperationCanceledException) { /* Expected on stop */ }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Driver->Net] Loop failed for {Config.ComPortName}: {ex.Message}");
        }
    }

    private async Task NetworkToDriverLoop(CancellationToken token)
    {
        var buffer = new byte[4096];
        try
        {
            while (!token.IsCancellationRequested)
            {
                var bytesRead = await _networkClient.ReceiveAsync(buffer, token);
                if (bytesRead > 0)
                {
                    var data = buffer.AsSpan(0, bytesRead).ToArray();
                    DataReceivedFromNetwork?.Invoke(data);
                    _driverClient.PushIncomingData(data, data.Length);
                }
                else
                {
                    // A zero-byte read indicates a graceful remote disconnect.
                    Debug.WriteLine($"Remote host closed the connection for {Config.ComPortName}.");
                    break;
                }
            }
        }
        catch (OperationCanceledException) { /* Expected on stop */ }
        catch (IOException ex) when (ex.InnerException is SocketException)
        {
            // This specifically catches forcible disconnects.
            Debug.WriteLine($"Connection forcibly closed by remote host for {Config.ComPortName}.");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Net->Driver] Loop failed for {Config.ComPortName}: {ex.Message}");
        }
    }

    private void UpdateStatus(ForwardingStatus status, string message)
    {
        Config.Status = status;
        StatusChanged?.Invoke(status, message);
        Debug.WriteLine($"[{Config.ComPortName}] Status: {status} - {message}");
    }
}