#include "common.h"

NTSTATUS DeviceCreate(
    _In_ WDFDRIVER Driver,
    _In_ PWDFDEVICE_INIT DeviceInit,
    _Out_ PDEVICE_CONTEXT* DeviceContext
)
{
	UNREFERENCED_PARAMETER(Driver);

    NTSTATUS status;
	WDF_OBJECT_ATTRIBUTES deviceAttributes;
	WDF_FILEOBJECT_CONFIG fileCfg;
    WDFDEVICE device;
	PDEVICE_CONTEXT pDeviceContext;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

	deviceAttributes.SynchronizationScope = WdfSynchronizationScopeDevice;
	deviceAttributes.EvtCleanupCallback = VcomEvtDeviceCleanup;

	WDF_FILEOBJECT_CONFIG_INIT(&fileCfg,
		VcomEvtFileCreate,   // EvtDeviceFileCreate
		VcomEvtFileClose,    // EvtFileClose
		VcomEvtFileCleanup); // EvtFileCleanup

	WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_SERIAL_PORT);
	WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

	WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileCfg, WDF_NO_OBJECT_ATTRIBUTES);

	
	WdfDeviceInitSetExclusive(DeviceInit, TRUE);

	status = WdfDeviceCreate(
		&DeviceInit, 
		&deviceAttributes, 
		&device);
	if (!NT_SUCCESS(status)) {
		KdPrint(("WdfDeviceCreate failed with status 0x%08X\n", status));
		return status;
	}
	pDeviceContext = GetDeviceContext(device);
	pDeviceContext->Device = device;

	// Safe defaults for a fresh virtual COM device
	pDeviceContext->BaudRate = 9600;
	pDeviceContext->ModemControlRegister = 0;
	pDeviceContext->FifoControlRegister = 0;
	pDeviceContext->LineControlRegister = (SERIAL_8_DATA | SERIAL_1_STOP | SERIAL_NONE_PARITY);
	pDeviceContext->ValidDataMask = 0xFF;
	RtlZeroMemory(&pDeviceContext->Timeouts, sizeof(pDeviceContext->Timeouts));
	pDeviceContext->FlowControl = 0;

	pDeviceContext->PdoName = NULL;
	pDeviceContext->bCreatedLegacyHardwareKey = FALSE;

	RtlZeroMemory(&pDeviceContext->Config, sizeof(pDeviceContext->Config));
	pDeviceContext->Config.TransportType = VcomTransportUdp;

	*DeviceContext = pDeviceContext;
	    
    return status;
}

NTSTATUS DeviceConfigure(
	_In_ PDEVICE_CONTEXT DeviceContext
){
	NTSTATUS status;
	WDFDEVICE device = DeviceContext->Device;
	WDFKEY registryKey = NULL;
	LPGUID guid;
	errno_t errorNo;

	DECLARE_CONST_UNICODE_STRING(portName, REG_VALUENAME_PORTNAME);
	DECLARE_UNICODE_STRING_SIZE(comPort, 10);
	DECLARE_UNICODE_STRING_SIZE(symbolicLinkName, SYMBOLIC_LINK_NAME_LENGTH);

	guid = (LPGUID)&GUID_DEVINTERFACE_COMPORT;

	status = WdfDeviceCreateDeviceInterface(
		device, 
		guid, 
		NULL);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Device Interface Creation failed with status 0x%08X\n", status));
		goto _exit;
	}

	status = WdfDeviceOpenRegistryKey(
		device, 
		PLUGPLAY_REGKEY_DEVICE, 
		KEY_QUERY_VALUE,
		WDF_NO_OBJECT_ATTRIBUTES,
		&registryKey);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to open Ports Reg Key with status 0x%08X\n", status));
		goto _exit;
	}
	status = WdfRegistryQueryUnicodeString(
		registryKey, 
		&portName,
		NULL,
		&comPort);
	if (!NT_SUCCESS(status)) {
		KdPrint(("PortName not found under device key (status 0x%08X). Using fallback.\n", status));
		RtlZeroMemory(comPort.Buffer, comPort.MaximumLength);
		RtlStringCchCopyW(comPort.Buffer, comPort.MaximumLength / sizeof(WCHAR), L"COM250");
		comPort.Length = (USHORT)(wcslen(comPort.Buffer) * sizeof(WCHAR));
		status = STATUS_SUCCESS; // continue
	}
	symbolicLinkName.Length = (USHORT)((wcslen(comPort.Buffer) * sizeof(wchar_t))
		+ sizeof(SYMBOLIC_LINK_NAME_PREFIX) - sizeof(UNICODE_NULL));

	if (symbolicLinkName.Length >= symbolicLinkName.MaximumLength) {

		KdPrint(("Error: Buffer overflow when creating COM port name.Size"
			" is %d, buffer length is %d", symbolicLinkName.Length, symbolicLinkName.MaximumLength));
		status = STATUS_BUFFER_OVERFLOW;
		goto _exit;
	}

	errorNo = wcscpy_s(symbolicLinkName.Buffer,
		SYMBOLIC_LINK_NAME_LENGTH,
		SYMBOLIC_LINK_NAME_PREFIX);

	if (errorNo != 0) {
		KdPrint(("Failed to copy %ws to buffer with error %d",
			SYMBOLIC_LINK_NAME_PREFIX, errorNo));
		status = STATUS_INVALID_PARAMETER;
		goto _exit;
	}

	errorNo = wcscat_s(symbolicLinkName.Buffer,
		SYMBOLIC_LINK_NAME_LENGTH,
		comPort.Buffer);

	if (errorNo != 0) {
		KdPrint((
			"Failed to copy %ws to buffer with error %d",
			comPort.Buffer, errorNo));
		status = STATUS_INVALID_PARAMETER;
		goto _exit;
	}

	status = WdfDeviceCreateSymbolicLink(
		device, 
		&symbolicLinkName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device symbolic link with status 0x%08X\n", status));
		goto _exit;
	}

	status = DeviceGetPdoName(DeviceContext);
	if(!NT_SUCCESS(status)) {
		KdPrint(("Failed to get PDO name with status 0x%08X\n", status));
		goto _exit;
	}
	status = DeviceWriteLegacyHardwareKey(
		DeviceContext->PdoName, 
		comPort.Buffer, 
		DeviceContext->Device);
	if (NT_SUCCESS(status)) {
		DeviceContext->bCreatedLegacyHardwareKey = TRUE;
	}
	status = QueueCreate(DeviceContext);
	if(!NT_SUCCESS(status)) {
		KdPrint(("Failed to create I/O queue with status 0x%08X\n", status));
		goto _exit;
	}
_exit:
	if(registryKey)	WdfRegistryClose(registryKey);
	return status;

}

NTSTATUS DeviceGetPdoName(
	_In_ PDEVICE_CONTEXT DeviceContext
)
{
	NTSTATUS status;
	WDFDEVICE device = DeviceContext->Device;
	WDF_OBJECT_ATTRIBUTES objAttr;
	WDFMEMORY memory;

	WDF_OBJECT_ATTRIBUTES_INIT(&objAttr);
	objAttr.ParentObject = device;
	status = WdfDeviceAllocAndQueryProperty(
		device, 
		DevicePropertyPhysicalDeviceObjectName, 
		NonPagedPoolNx, 
		&objAttr, 
		&memory);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to query PDO name with status 0x%08X\n", status));
		goto _exit;
	}
	DeviceContext->PdoName = (PWCHAR)WdfMemoryGetBuffer(memory, NULL);
	if (DeviceContext->PdoName == NULL) {
		KdPrint(("Failed to get PDO name buffer\n"));
		status = STATUS_UNSUCCESSFUL;
		goto _exit;
	}
	KdPrint(("PDO Name: %ws\n", DeviceContext->PdoName));
_exit:
	return status;
}

NTSTATUS DeviceWriteLegacyHardwareKey(
	_In_ PWSTR PdoName,
	_In_ PWSTR ComPort,
	_In_ WDFDEVICE Device
	)
{
	WDFKEY key = NULL;
	NTSTATUS status;
	UNICODE_STRING pdoString = { 0 };
	UNICODE_STRING comPort = { 0 };

	DECLARE_CONST_UNICODE_STRING(deviceSubkey, SERIAL_DEVICE_MAP);

	RtlInitUnicodeString(&pdoString, PdoName);
	RtlInitUnicodeString(&comPort, ComPort);

	status = WdfDeviceOpenDevicemapKey(
		Device, 
		&deviceSubkey, 
		KEY_SET_VALUE, 
		WDF_NO_OBJECT_ATTRIBUTES, 
		&key);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to open device map key with status 0x%08X\n", status));
		goto _exit;
	}
	status = WdfRegistryAssignUnicodeString(
		key, 
		&pdoString, 
		&comPort);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to assign unicode string with status 0x%08X\n", status));
		goto _exit;
	}
_exit:
	if(key) {
		WdfRegistryClose(key);
		key = NULL;
	}
	return status;
}

VOID VcomEvtDeviceCleanup(
	_In_ WDFOBJECT Device
)
{
	WDFDEVICE device = (WDFDEVICE)Device;
	PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
	NTSTATUS status;
	WDFKEY key = NULL;
	UNICODE_STRING PdoString = { 0 };

	DECLARE_CONST_UNICODE_STRING(deviceSubkey, SERIAL_DEVICE_MAP);

	if (deviceContext->bCreatedLegacyHardwareKey== TRUE && deviceContext->PdoName) {

		RtlInitUnicodeString(&PdoString, deviceContext->PdoName);

		status = WdfDeviceOpenDevicemapKey(
			device,
			&deviceSubkey,
			KEY_SET_VALUE,
			WDF_NO_OBJECT_ATTRIBUTES,
			&key);

		if (!NT_SUCCESS(status)) {
			KdPrint(("Error: Failed to open DEVICEMAP\\SERIALCOMM key 0x%x", status));
			goto _exit;
		}

		status = WdfRegistryRemoveValue(
			key,
			&PdoString);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Error: Failed to delete %S key, 0x%x", PdoString.Buffer, status));
			goto _exit;
		}
	}
_exit:

	if (key != NULL) {
		WdfRegistryClose(key);
		key = NULL;
	}
	return;

}

VOID
VcomEvtFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
	WDFDEVICE device = WdfFileObjectGetDevice(FileObject);
	PDEVICE_CONTEXT dev = GetDeviceContext(device);

	// Reach our queue state
	if (dev->IoQueue) {
		PQUEUE_CONTEXT qc = GetQueueContext(dev->IoQueue);

		// Cancel/purge any pending requests from this file (single-open, so safe)
		WdfIoQueuePurgeSynchronously(qc->ReadQueue);
		WdfIoQueuePurgeSynchronously(qc->OutgoingQueue);
		WdfIoQueuePurgeSynchronously(qc->WaitMaskQueue);

		// Reset rings
		WdfSpinLockAcquire(qc->RingBufferFromNetworkLock);
		RingBufferReset(&qc->RingBufferFromNetwork);
		WdfSpinLockRelease(qc->RingBufferFromNetworkLock);

		WdfSpinLockAcquire(qc->RingBufferToUserModeLock);
		RingBufferReset(&qc->RingBufferToUserMode);
		WdfSpinLockRelease(qc->RingBufferToUserModeLock);
	}
}

VOID
VcomEvtFileClose(_In_ WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(FileObject);
	// Nothing else to do; Cleanup already purged/cleared
}

VOID
VcomEvtFileCreate(
	_In_ WDFDEVICE     Device,
	_In_ WDFREQUEST    Request,
	_In_ WDFFILEOBJECT FileObject
)
{
	UNREFERENCED_PARAMETER(Device);
	UNREFERENCED_PARAMETER(FileObject);
	// With Exclusive=TRUE the framework enforces single-open for us.
	WdfRequestComplete(Request, STATUS_SUCCESS);
}

ULONG GetBaudRate(_In_ PDEVICE_CONTEXT Ctx) {
	return (ULONG)ReadNoFence((LONG*)&Ctx->BaudRate);
}

VOID SetBaudRate(
	_Inout_ PDEVICE_CONTEXT Ctx,
	_In_ ULONG BaudRate
) {
	InterlockedExchange((LONG*)&Ctx->BaudRate, (LONG)BaudRate);
}
PULONG GetModemControlRegister(
	_Inout_ PDEVICE_CONTEXT Ctx
) { return &Ctx->ModemControlRegister; }

PULONG GetFifoControlRegisterPtr(
	_Inout_ PDEVICE_CONTEXT Ctx) {
	return &Ctx->FifoControlRegister;
}

PULONG GetLineControlRegisterPtr(
	_Inout_ PDEVICE_CONTEXT Ctx) {
	return &Ctx->LineControlRegister;
}

VOID   SetValidDataMask(
	_Inout_ PDEVICE_CONTEXT Ctx,
	_In_ UCHAR Mask) {
	Ctx->ValidDataMask = Mask;
}

VOID   SetTimeouts(
	_Inout_ PDEVICE_CONTEXT Ctx,
	_In_ SERIAL_TIMEOUTS To) {
	Ctx->Timeouts = To;
}


