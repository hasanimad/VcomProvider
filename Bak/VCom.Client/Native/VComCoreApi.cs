using System;
using System.Runtime.InteropServices;
using System.Text;

namespace VCom.Client.Native;

// These enums must match the C++ versions exactly.
public enum ForwardingStatus
{
    Idle, Running, Connected, Error
}
public enum ProtocolType
{
    Tcp, Udp
}

internal static class VComCoreApi
{
    // The name of our C++ DLL.
    private const string DllName = "VCom.Core.Cpp.dll";

    // Define the C# delegates for our callbacks. The UnmanagedFunctionPointer attribute
    // tells .NET how to correctly pass these to the native C++ code.
    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
    public delegate void StatusCallback(string devicePath, ForwardingStatus status, string message);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate void DataCallback(string devicePath, IntPtr data, int dataLength, int direction);

    // --- DllImport for each function in our C++ API ---

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void VCom_Initialize(StatusCallback statusCb, DataCallback dataCb);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void VCom_Shutdown();

    // The return value is a pointer to a string that we must free manually.
    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern IntPtr VCom_GetDeviceList_Internal();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern void VCom_FreeString(IntPtr str);

    // This is a helper method to safely handle getting the string and freeing the memory.
    public static string VCom_GetDeviceList()
    {
        var ptr = VCom_GetDeviceList_Internal();
        try
        {
            return Marshal.PtrToStringUni(ptr) ?? "[]";
        }
        finally
        {
            if (ptr != IntPtr.Zero)
            {
                VCom_FreeString(ptr);
            }
        }
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern bool VCom_StartForwarding(string devicePath, string host, int port, ProtocolType protocol);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void VCom_StopForwarding(string devicePath);
}