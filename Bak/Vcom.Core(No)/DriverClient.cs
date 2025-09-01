using Microsoft.Win32.SafeHandles;
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace VCom.Core;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
public class DriverClient : IDisposable
{
    private SafeFileHandle? _deviceHandle;
    private readonly string _devicePath; // This is the unique path from the GUID

    public bool IsConnected => _deviceHandle != null && !_deviceHandle.IsInvalid;

    // CORRECT: The constructor takes the full device interface path.
    public DriverClient(string devicePath)
    {
        _devicePath = devicePath ?? throw new ArgumentNullException(nameof(devicePath));
    }

    public void Open()
    {
        if (IsConnected) return;

        // CORRECT: Use the device interface path for CreateFile.
        _deviceHandle = NativeMethods.CreateFile(
            _devicePath,
            FileAccess.ReadWrite,
            FileShare.None,
            IntPtr.Zero,
            FileMode.Open,
            NativeMethods.FILE_ATTRIBUTE_NORMAL | NativeMethods.FILE_FLAG_OVERLAPPED,
            IntPtr.Zero);

        if (_deviceHandle.IsInvalid)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"Failed to open device handle for '{_devicePath}'.");
        }
    }

    // ... The rest of the methods (Start, Stop, GetOutgoingData, etc.) are unchanged ...
    // They will correctly use the handle obtained from the device path.

    public void Start()
    {
        if (!IsConnected) throw new InvalidOperationException("Device is not open.");
        SendControlCommand(NativeMethods.IOCTL_VCOM_START);
    }

    public void Stop()
    {
        if (!IsConnected) return;
        try { SendControlCommand(NativeMethods.IOCTL_VCOM_STOP); }
        catch (Win32Exception ex) { System.Diagnostics.Debug.WriteLine($"Ignoring error during driver Stop IOCTL: {ex.Message}"); }
    }

    public unsafe int GetOutgoingData(byte[] buffer, CancellationToken cancellationToken)
    {
        if (!IsConnected) throw new InvalidOperationException("Device is not open.");
        var overlapped = new Overlapped();
        var nativeOverlapped = overlapped.Pack(null, null);
        try
        {
            using (cancellationToken.Register(() => NativeMethods.CancelIoEx(_deviceHandle!, (IntPtr)nativeOverlapped)))
            {
                bool success = NativeMethods.DeviceIoControl(
                    _deviceHandle!, NativeMethods.IOCTL_VCOM_GET_OUTGOING,
                    IntPtr.Zero, 0, buffer, (uint)buffer.Length,
                    out uint bytesReturned, nativeOverlapped);
                if (!success)
                {
                    int error = Marshal.GetLastWin32Error();
                    if (error == 995) return 0;
                    if (error == 997)
                    {
                        success = NativeMethods.GetOverlappedResult(_deviceHandle!, nativeOverlapped, out bytesReturned, true);
                        if (!success)
                        {
                            error = Marshal.GetLastWin32Error();
                            if (error == 995) return 0;
                            throw new Win32Exception(error, "GetOverlappedResult failed for GET_OUTGOING.");
                        }
                    }
                    else { throw new Win32Exception(error, "DeviceIoControl failed for GET_OUTGOING."); }
                }
                return (int)bytesReturned;
            }
        }
        finally { Overlapped.Free(nativeOverlapped); }
    }

    public void PushIncomingData(byte[] data, int count)
    {
        if (!IsConnected) throw new InvalidOperationException("Device is not open.");
        if (count == 0) return;
        bool success = NativeMethods.DeviceIoControl(
            _deviceHandle!, NativeMethods.IOCTL_VCOM_PUSH_INCOMING,
            data, (uint)count, IntPtr.Zero, 0, out _, IntPtr.Zero);
        if (!success) { throw new Win32Exception(Marshal.GetLastWin32Error(), "DeviceIoControl failed for PUSH_INCOMING."); }
    }

    private void SendControlCommand(uint controlCode)
    {
        bool success = NativeMethods.DeviceIoControl(
            _deviceHandle!, controlCode, IntPtr.Zero, 0, IntPtr.Zero, 0, out _, IntPtr.Zero);
        if (!success) { throw new Win32Exception(Marshal.GetLastWin32Error(), $"Failed to send IOCTL: 0x{controlCode:X}."); }
    }

    public void Dispose()
    {
        Stop();
        _deviceHandle?.Close();
        _deviceHandle = null;
    }
}