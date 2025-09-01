using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace VCom.Core;

/// <summary>
/// Contains all P/Invoke definitions and constants required for communication
/// with the driver and for device management via the SetupAPI.
/// This is the single source of truth for all native interoperability.
/// </summary>
internal static class NativeMethods
{
    #region GUIDs and IOCTLs from public.h and vcom.inf

    // Custom control interface GUID from public.h
    public static readonly Guid GUID_DEVINTERFACE_VCOM_CONTROL = new("{8E25B239-36F1-4568-B6A5-69A6272555E7}");

    // Standard GUID for the "Ports (COM & LPT)" device class from vcom.inf
    public static readonly Guid GUID_DEVCLASS_PORTS = new("4d36e978-e325-11ce-bfc1-08002be10318");

    // IOCTL codes from public.h
    public const uint IOCTL_VCOM_GET_OUTGOING = 0x80002004; // Corrected based on CTL_CODE macro common usage
    public const uint IOCTL_VCOM_PUSH_INCOMING = 0x80002008; // Corrected based on CTL_CODE macro common usage
    public const uint IOCTL_VCOM_START = 0x8000200C;
    public const uint IOCTL_VCOM_STOP = 0x80002010;

    #endregion

    #region Win32 Constants

    public static readonly IntPtr INVALID_HANDLE_VALUE = new(-1);
    public const int ERROR_INSUFFICIENT_BUFFER = 122;
    public const int ERROR_NO_MORE_ITEMS = 259;
    public const int ERROR_IO_PENDING = 997;
    public const int ERROR_OPERATION_ABORTED = 995;

    public const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    public const uint FILE_FLAG_OVERLAPPED = 0x40000000;

    public const uint DIGCF_PRESENT = 0x00000002;
    public const uint DIGCF_DEVICEINTERFACE = 0x00000010;
    public const uint DICD_GENERATE_ID = 0x00000001;
    public const uint DI_REMOVEDEVICE_GLOBAL = 0x00000001;

    public const int DICS_FLAG_GLOBAL = 0x00000001;
    public const int DIREG_DEV = 0x00000001;
    public const int KEY_SET_VALUE = 0x0002;

    #endregion

    #region Structures

    [StructLayout(LayoutKind.Sequential)]
    internal struct SP_DEVINFO_DATA
    {
        public uint cbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SP_DEVICE_INTERFACE_DATA
    {
        public uint cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public IntPtr Reserved;
    }

    // This structure is a placeholder for a variable-sized buffer.
    // The first field, cbSize, is the only fixed part. The DevicePath follows it.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    internal struct SP_DEVICE_INTERFACE_DETAIL_DATA
    {
        public uint cbSize;
        // The DevicePath is a variable-length string that starts here.
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SP_CLASSINSTALL_HEADER
    {
        public uint cbSize;
        public uint InstallFunction;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SP_REMOVEDEVICE_PARAMS
    {
        public SP_CLASSINSTALL_HEADER ClassInstallHeader;
        public uint Scope;
        public uint HwProfile;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DEVPROPKEY
    {
        public Guid fmtid;
        public uint pid;
    }

    // The property key for the device interface's reference string.
    internal static DEVPROPKEY DEVPKEY_DeviceInterface_ReferenceString = new() { fmtid = new Guid("026e516e-b814-414b-83cd-856d6fef4822"), pid = 2 };

    #endregion

    #region Enums

    internal enum DiClassInstallFunction : uint
    {
        DIF_REGISTERDEVICE = 0x00000019,
        DIF_REMOVE = 0x00000005,
    }

    internal enum SPDRP : uint
    {
        SPDRP_DEVICEDESC = 0x00000000, 
        SPDRP_HARDWAREID = 0x00000001,
        SPDRP_ENUMERATOR_NAME = 0x00000016
    }

    #endregion

    #region Kernel32.dll Functions

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern SafeFileHandle CreateFile(
        string lpFileName,
        [MarshalAs(UnmanagedType.U4)] FileAccess dwDesiredAccess,
        [MarshalAs(UnmanagedType.U4)] FileShare dwShareMode,
        IntPtr lpSecurityAttributes,
        [MarshalAs(UnmanagedType.U4)] FileMode dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern unsafe bool DeviceIoControl(
        SafeFileHandle safeFileHandle, 
        uint iOCTL_VCOM_GET_OUTGOING, 
        nint zero, 
        int v, 
        byte[] buffer, 
        uint length, 
        out uint bytesReturned, 
        NativeOverlapped* nativeOverlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        SafeFileHandle hDevice,
        uint dwIoControlCode,
        IntPtr lpInBuffer,
        uint nInBufferSize,
        IntPtr lpOutBuffer,
        uint nOutBufferSize,
        out uint lpBytesReturned,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        SafeFileHandle hDevice,
        uint dwIoControlCode,
        [In] byte[] lpInBuffer,
        uint nInBufferSize,
        IntPtr lpOutBuffer,
        uint nOutBufferSize,
        out uint lpBytesReturned,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        SafeFileHandle hDevice,
        uint dwIoControlCode,
        IntPtr lpInBuffer,
        uint nInBufferSize,
        [Out] byte[] lpOutBuffer,
        uint nOutBufferSize,
        out uint lpBytesReturned,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern unsafe bool GetOverlappedResult(
        SafeFileHandle hFile,
        [In] NativeOverlapped* lpOverlapped,
        out uint lpNumberOfBytesTransferred,
        [In, MarshalAs(UnmanagedType.Bool)] bool bWait);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CancelIoEx(SafeFileHandle hFile, IntPtr lpOverlapped);

    #endregion

    #region SetupApi.dll Functions
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SetupDiOpenDevRegKey(
       IntPtr deviceInfoSet,
       ref SP_DEVINFO_DATA deviceInfoData,
       uint scope,
       uint hwProfile,
       uint keyType,
       uint samDesired);
    [DllImport("setupapi.dll", SetLastError = true)]
    internal static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid ClassGuid, IntPtr hwndParent);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    internal static extern bool SetupDiCreateDeviceInfo(
        IntPtr DeviceInfoSet,
        string DeviceName,
        ref Guid ClassGuid,
        string DeviceDescription,
        IntPtr hwndParent,
        uint CreationFlags,
        ref SP_DEVINFO_DATA DeviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    internal static extern bool SetupDiSetDeviceRegistryProperty(
        IntPtr DeviceInfoSet,
        ref SP_DEVINFO_DATA DeviceInfoData,
        SPDRP Property,
        [In] byte[] PropertyBuffer,
        uint PropertyBufferSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    internal static extern bool SetupDiCallClassInstaller(
        DiClassInstallFunction InstallFunction,
        IntPtr DeviceInfoSet,
        ref SP_DEVINFO_DATA DeviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr SetupDiGetClassDevs(
        ref Guid ClassGuid,
        string? Enumerator,
        IntPtr hwndParent,
        uint Flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiEnumDeviceInterfaces(
        IntPtr hDevInfo,
        IntPtr devInfo,
        ref Guid interfaceClassGuid,
        uint memberIndex,
        ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(
       IntPtr hDevInfo,
       ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData,
       IntPtr deviceInterfaceDetailData,
       uint deviceInterfaceDetailDataSize,
       out uint requiredSize,
       IntPtr deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern bool SetupDiGetDeviceInterfaceDetail(
       IntPtr hDevInfo,
       ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData,
       IntPtr deviceInterfaceDetailData,
       uint deviceInterfaceDetailDataSize,
       IntPtr requiredSize,
       ref SP_DEVINFO_DATA deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern bool SetupDiGetDeviceInterfaceProperty(
        IntPtr deviceInfoSet,
        ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData,
        ref DEVPROPKEY propertyKey,
        out uint propertyType,
        IntPtr propertyBuffer,
        uint propertyBufferSize,
        out uint requiredSize,
        uint flags);

    [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Auto)]
    internal static extern bool SetupDiOpenDeviceInterface(
        IntPtr hDevInfo,
        string devicePath,
        uint openFlags,
        ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", SetLastError = true)]
    internal static extern bool SetupDiSetClassInstallParams(
        IntPtr DeviceInfoSet,
        ref SP_DEVINFO_DATA DeviceInfoData,
        ref SP_REMOVEDEVICE_PARAMS ClassInstallParams,
        uint ClassInstallParamsSize);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr hDevInfo);

    #endregion
    #region advapi32.dll
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern int RegSetValueEx(
       IntPtr hKey,
       string lpValueName,
       int Reserved,
       RegistryValueKind dwType,
       byte[] lpData,
       int cbData);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern int RegCloseKey(IntPtr hKey);
    #endregion
}
