#include "../../Utils/pch.h"
#include "../inc/vcomctl.h"

EVT_WDF_DRIVER_DEVICE_ADD VcomEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VcomEvtIoDeviceControl;

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT pDriverObject, PUNICODE_STRING uRegistryPath) {
	::KdPrint(("VcomDriver: Entered DriverEntry\n"));

	WDF_DRIVER_CONFIG config;
	NTSTATUS status = STATUS_FAILED_DRIVER_ENTRY;

	::KdPrint(("VcomDriver: WDF Driver Config Init...\n"));
	::WDF_DRIVER_CONFIG_INIT(&config, VcomEvtDeviceAdd);

	::KdPrint(("VcomDriver: WDF Driver Creation...\n"));
	status = ::WdfDriverCreate(
		pDriverObject,
		uRegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status)) {
		::KdPrint(("VcomDriver: WdfDriverCreate failed with status 0x%08X\n", status));
		return status;
	}

	::KdPrint(("VcomDriver: DriverEntry completed successfully\n"));
	return status;
}

NTSTATUS VcomEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit) {
	::KdPrint(("VcomDriver: Entered Evt Device Add Callback\n"));

	UNREFERENCED_PARAMETER(Driver);

	NTSTATUS			status		= STATUS_SUCCESS;
	WDFDEVICE			wdfDevice	= nullptr;
	WDF_IO_QUEUE_CONFIG ioQueueConfig = { 0 };

	::KdPrint(("VcomDriver: Wdf Setting Execlusive Device to False...\n"));
	::WdfDeviceInitSetExclusive(DeviceInit, FALSE);

	::KdPrint(("VcomDriver: Wdf Device Creation...\n"));
	status = ::WdfDeviceCreate(
		&DeviceInit, 
		WDF_NO_OBJECT_ATTRIBUTES, 
		&wdfDevice);

	if(!NT_SUCCESS(status)) {
		::KdPrint(("VcomDriver: WdfDeviceCreate failed with status 0x%08X\n", status));
		return status;
	}

	::KdPrint(("VcomDriver: Wdf IO Queue Config Init...\n"));
	::WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchSequential);
	ioQueueConfig.EvtIoDeviceControl = VcomEvtIoDeviceControl;

	::KdPrint(("VcomDriver: Wdf Io Queue Creation....\n"));
	status = ::WdfIoQueueCreate(
		wdfDevice,
		&ioQueueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		WDF_NO_HANDLE
	);
	if(!NT_SUCCESS(status)) {
		::KdPrint(("VcomDriver: WdfIoQueueCreate failed with status 0x%08X\n", status));
		return status;
	}

	KdPrint(("VcomDriver: Device created successfully\n"));
	return status;
}

VOID VcomEvtIoDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
) {
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(IoControlCode);

	::WdfRequestCompleteWithInformation(
		Request,
		STATUS_NOT_SUPPORTED,
		0
	);
}