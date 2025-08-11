#include "../../Utils/pch.h"
#include "../inc/vcomctl.h"

EVT_WDF_DRIVER_DEVICE_ADD VcomEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VcomEvtIoDeviceControl;

extern "C"
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT pDriverObject, PUNICODE_STRING uRegistryPath) {
	NTSTATUS status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG driverConfig = { 0 };

	WDF_DRIVER_CONFIG_INIT(&driverConfig, VcomEvtDeviceAdd);

	status = WdfDriverCreate(
		pDriverObject,
		uRegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&driverConfig,
		WDF_NO_HANDLE
	);
	if (!NT_SUCCESS(status)) {
		::KdPrint(("VcomDriver: WdfDriverCreate failed with status 0x%08X\n", status));
		return status;
	}
	return status;
}

NTSTATUS VcomEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit) {
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_CONTEXT pDeviceContext = nullptr;

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