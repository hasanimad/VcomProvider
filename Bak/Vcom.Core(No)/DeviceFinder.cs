using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using VCom.Core.Models;

namespace VCom.Core;

/// <summary>
/// Helper class to find all active VCOM devices by enumerating the driver's
/// custom control interface GUID. This version includes detailed logging.
/// </summary>
public static class DeviceFinder
{
    public static List<PortConfig> GetVComPorts()
    {
        Debug.WriteLine("[DeviceFinder] Starting device enumeration...");
        var devices = new List<PortConfig>();
        var controlGuid = NativeMethods.GUID_DEVINTERFACE_VCOM_CONTROL;
        var deviceInfoSet = IntPtr.Zero;

        try
        {
            deviceInfoSet = NativeMethods.SetupDiGetClassDevs(
                ref controlGuid,
                null,
                IntPtr.Zero,
                NativeMethods.DIGCF_PRESENT | NativeMethods.DIGCF_DEVICEINTERFACE);

            if (deviceInfoSet == NativeMethods.INVALID_HANDLE_VALUE)
            {
                Debug.WriteLine("[DeviceFinder] SetupDiGetClassDevs failed or no devices found. Ending enumeration.");
                return devices;
            }
            Debug.WriteLine($"[DeviceFinder] SetupDiGetClassDevs successful. DeviceInfoSet handle: {deviceInfoSet}.");

            uint memberIndex = 0;
            while (true)
            {
                var deviceInterfaceData = new NativeMethods.SP_DEVICE_INTERFACE_DATA();
                deviceInterfaceData.cbSize = (uint)Marshal.SizeOf(deviceInterfaceData);

                if (!NativeMethods.SetupDiEnumDeviceInterfaces(deviceInfoSet, IntPtr.Zero, ref controlGuid, memberIndex, ref deviceInterfaceData))
                {
                    var error = Marshal.GetLastWin32Error();
                    if (error == NativeMethods.ERROR_NO_MORE_ITEMS)
                    {
                        Debug.WriteLine("[DeviceFinder] SetupDiEnumDeviceInterfaces: No more items. Enumeration complete.");
                        break;
                    }
                    Debug.WriteLine($"[DeviceFinder] SetupDiEnumDeviceInterfaces failed with error code {error}. Stopping.");
                    throw new Win32Exception(error, "Failed to enumerate device interfaces.");
                }
                Debug.WriteLine($"[DeviceFinder] Found device interface at index {memberIndex}.");

                var portInfo = GetPortInfo(deviceInfoSet, ref deviceInterfaceData);
                if (portInfo != null)
                {
                    Debug.WriteLine($"[DeviceFinder] Successfully retrieved info for {portInfo.ComPortName} ({portInfo.DevicePath})");
                    devices.Add(portInfo);
                }

                memberIndex++;
            }
        }
        finally
        {
            if (deviceInfoSet != IntPtr.Zero && deviceInfoSet != NativeMethods.INVALID_HANDLE_VALUE)
            {
                NativeMethods.SetupDiDestroyDeviceInfoList(deviceInfoSet);
                Debug.WriteLine("[DeviceFinder] Cleaned up DeviceInfoSet.");
            }
        }
        Debug.WriteLine($"[DeviceFinder] Found {devices.Count} total devices.");
        return devices;
    }
    private static string ExtractComNameFromPath(string devicePath)
    {
        if (string.IsNullOrWhiteSpace(devicePath)) return string.Empty;
        // devicePath example: "\\?\\root#ports#0000#{...}\\com3"
        var parts = devicePath.Split(new[] { '\\' }, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0) return string.Empty;
        var last = parts[^1];                 // "com3"
        return last.StartsWith("com", StringComparison.OrdinalIgnoreCase)
            ? last.ToUpperInvariant()         // "COM3"
            : last;                           // fallback
    }
    private static PortConfig? GetPortInfo(IntPtr deviceInfoSet, ref NativeMethods.SP_DEVICE_INTERFACE_DATA deviceInterfaceData)
    {
        try
        {
            var devicePath = GetDevicePath(deviceInfoSet, ref deviceInterfaceData);
            var comPortName = ExtractComNameFromPath(devicePath);

            if (!string.IsNullOrEmpty(devicePath) && !string.IsNullOrEmpty(comPortName))
            {
                return new PortConfig { ComPortName = comPortName, DevicePath = devicePath };
            }
            Debug.WriteLine("[DeviceFinder] GetPortInfo failed: either device path or COM port name was empty.");
        }
        catch (Win32Exception ex)
        {
            Debug.WriteLine($"[DeviceFinder] GetPortInfo caught an exception: {ex.Message} (Code: {ex.NativeErrorCode})");
        }
        return null;
    }

    private static string GetDevicePath(IntPtr deviceInfoSet, ref NativeMethods.SP_DEVICE_INTERFACE_DATA deviceInterfaceData)
    {
        Debug.WriteLine("[DeviceFinder] Getting device path...");
        NativeMethods.SetupDiGetDeviceInterfaceDetail(deviceInfoSet, ref deviceInterfaceData, IntPtr.Zero, 0, out var requiredSize, IntPtr.Zero);
        if (requiredSize == 0)
        {
            Debug.WriteLine("[DeviceFinder] GetDevicePath: required size is 0. Aborting.");
            return string.Empty;
        }

        var detailDataBuffer = Marshal.AllocHGlobal((int)requiredSize);
        try
        {
            Marshal.WriteInt32(detailDataBuffer, (IntPtr.Size == 8) ? 8 : 5);
            if (!NativeMethods.SetupDiGetDeviceInterfaceDetail(deviceInfoSet, ref deviceInterfaceData, detailDataBuffer, requiredSize, out _, IntPtr.Zero))
            {
                throw new Win32Exception("Failed to get device interface detail on second call.");
            }
            var devicePath = Marshal.PtrToStringAuto(new IntPtr(detailDataBuffer.ToInt64() + 4)) ?? string.Empty;
            Debug.WriteLine($"[DeviceFinder] GetDevicePath successful: {devicePath}");
            return devicePath;
        }
        finally
        {
            Marshal.FreeHGlobal(detailDataBuffer);
        }
    }

    private static string GetReferenceString(IntPtr deviceInfoSet, ref NativeMethods.SP_DEVICE_INTERFACE_DATA deviceInterfaceData)
    {
        Debug.WriteLine("[DeviceFinder] Getting reference string (COM port)...");
        NativeMethods.SetupDiGetDeviceInterfaceProperty(deviceInfoSet, ref deviceInterfaceData, ref NativeMethods.DEVPKEY_DeviceInterface_ReferenceString,
            out _, IntPtr.Zero, 0, out var requiredSize, 0);

        if (requiredSize == 0)
        {
            Debug.WriteLine("[DeviceFinder] GetReferenceString: required size is 0. Aborting.");
            return string.Empty;
        }

        var propBuffer = Marshal.AllocHGlobal((int)requiredSize);
        try
        {
            if (!NativeMethods.SetupDiGetDeviceInterfaceProperty(deviceInfoSet, ref deviceInterfaceData, ref NativeMethods.DEVPKEY_DeviceInterface_ReferenceString,
                out _, propBuffer, requiredSize, out _, 0))
            {
                throw new Win32Exception("Failed to get device interface property (ReferenceString) on second call.");
            }
            var comPort = Marshal.PtrToStringUni(propBuffer) ?? string.Empty;
            Debug.WriteLine($"[DeviceFinder] GetReferenceString successful: {comPort}");
            return comPort;
        }
        finally
        {
            Marshal.FreeHGlobal(propBuffer);
        }
    }
}
