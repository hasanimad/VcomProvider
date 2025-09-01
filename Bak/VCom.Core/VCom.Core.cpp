#include "pch.h"
#include "VCom.Core.h"
#include "../VcomProviderV2/public.h"

#pragma comment(lib, "Setupapi.lib")




struct DeviceInfo {
	wchar_t comName[MAX_PATH];
	wchar_t devicePath[MAX_PATH*2];
};


VCOMCORE_API INT VcomGetDeviceCount(DeviceInfo* devInfoArray, int MaxDevices) {
    HDEVINFO hDevInfo;
    SP_DEVICE_INTERFACE_DATA devInterfaceData;
    DWORD interFaceIndex = 0;
    INT devCount = 0;

    hDevInfo = ::SetupDiGetClassDevs(&GUID_DEVINTERFACE_VCOM_CONTROL, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        ::OutputDebugString(L"Failed to get device information set.\n");
        return -1;

    }
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_VCOM_CONTROL, interFaceIndex, &devInterfaceData)) {
        if (devCount >= MaxDevices) {
            break; // Stop if the provided array is full
        }
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA devInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        if (devInterfaceDetailData == NULL) {
            continue; // Memory allocation failed
        }

        devInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        // Second call to get the actual device interface detail data
        if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, devInterfaceDetailData, requiredSize, NULL, &devInfoData)) {
            // Copy the device path to our struct
            wcscpy_s(devInfoArray[devCount].devicePath, 512, devInterfaceDetailData->DevicePath);

            // --- Get the friendly COM port name ---
            HKEY hKey;
            hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hKey != INVALID_HANDLE_VALUE) {
                wchar_t portName[256];
                DWORD size = sizeof(portName);
                DWORD type;

                if (RegQueryValueEx(hKey, L"PortName", NULL, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS) {
                    wcscpy_s(devInfoArray[devCount].comName, 256, portName);
                }
                else {
                    wcscpy_s(devInfoArray[devCount].comName, 256, L"Unknown");
                }
                RegCloseKey(hKey);
            }
            else {
                wcscpy_s(devInfoArray[devCount].comName, 256, L"Unknown");
            }

            devCount++;
        }

        free(devInterfaceDetailData);
        interFaceIndex++;
    }

    // Clean up the device information set handle
    SetupDiDestroyDeviceInfoList(hDevInfo);

    return devCount;
	}