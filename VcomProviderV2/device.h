#pragma once


// Symbolic link info for your virtual COM ports
#define SYMBOLIC_LINK_NAME_LENGTH   64
#define SYMBOLIC_LINK_NAME_PREFIX   L"\\DosDevices\\Global\\"

// Registry paths to map COM port names (used for legacy compatibility)
#define REG_PATH_DEVICEMAP          L"HARDWARE\\DEVICEMAP"
#define SERIAL_DEVICE_MAP           L"SERIALCOMM"
#define REG_VALUENAME_PORTNAME      L"PortName"
#define REG_PATH_SERIALCOMM         REG_PATH_DEVICEMAP L"\\" SERIAL_DEVICE_MAP

typedef struct _DEVICE_CONTEXT {
	WDFDEVICE Device; 
	
	WDFQUEUE IoQueue; // To clean up the queue on device close
	
	ULONG           BaudRate;
	ULONG           ModemControlRegister;
	ULONG           FifoControlRegister;
	ULONG           LineControlRegister;
	UCHAR           ValidDataMask;
	SERIAL_TIMEOUTS Timeouts;
	UCHAR FlowControl;

	// PDO /\ Reg Info
	PWSTR PdoName;
	BOOLEAN bCreatedLegacyHardwareKey;

	// Networking Specific State
	VCOM_CONFIG Config;

} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

EVT_WDF_DEVICE_FILE_CREATE VcomEvtFileCreate;
EVT_WDF_FILE_CLOSE         VcomEvtFileClose;
EVT_WDF_FILE_CLEANUP       VcomEvtFileCleanup;

NTSTATUS 
DeviceCreate(
	_In_ WDFDRIVER Driver,
	_In_ PWDFDEVICE_INIT DeviceInit,
	_Out_ PDEVICE_CONTEXT* DeviceContext
);

NTSTATUS
DeviceConfigure(
	_In_  PDEVICE_CONTEXT   DeviceContext
);

EVT_WDF_DEVICE_CONTEXT_CLEANUP  VcomEvtDeviceCleanup;

NTSTATUS DeviceGetPdoName(
	_In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS DeviceWriteLegacyHardwareKey(
	_In_ PWSTR PdoName, 
	_In_ PWSTR ComPort,
	_In_ WDFDEVICE Device);

ULONG GetBaudRate(
	_In_ PDEVICE_CONTEXT Ctx
);

VOID SetBaudRate(
	_Inout_ PDEVICE_CONTEXT Ctx,
	_In_ ULONG BaudRate
);

PULONG GetModemControlRegister(
	_Inout_ PDEVICE_CONTEXT Ctx
);

PULONG GetFifoControlRegisterPtr(
	_Inout_ PDEVICE_CONTEXT Ctx);

PULONG GetLineControlRegisterPtr(
	_Inout_ PDEVICE_CONTEXT Ctx);

VOID   SetValidDataMask(
	_Inout_ PDEVICE_CONTEXT Ctx, 
	_In_ UCHAR Mask);

VOID   SetTimeouts(
	_Inout_ PDEVICE_CONTEXT Ctx, 
	_In_ SERIAL_TIMEOUTS To);
