#pragma once

#ifdef VCOMCORE_EXPORTS
#define VCOMCORE_API __declspec(dllexport)
#else
#define VCOMCORE_API __declspec(dllimport)
#endif

#include <windows.h>

// The extern "C" is crucial for C# interop as it prevents C++ name mangling.
extern "C" {

    // We've added "__stdcall" to each function definition.
	VCOMCORE_API INT    __stdcall VcomGetDeviceCount();
    VCOMCORE_API HANDLE __stdcall VcomConnect();
    VCOMCORE_API void   __stdcall VcomDisconnect(HANDLE hDevice);
    VCOMCORE_API BOOL   __stdcall VcomStart(HANDLE hDevice);
    VCOMCORE_API BOOL   __stdcall VcomStop(HANDLE hDevice);
    VCOMCORE_API BOOL   __stdcall VcomRead(HANDLE hDevice, PVOID buffer, DWORD bufferSize, PDWORD bytesRead);
    VCOMCORE_API BOOL   __stdcall VcomWrite(HANDLE hDevice, PVOID buffer, DWORD bufferSize, PDWORD bytesWritten);
}