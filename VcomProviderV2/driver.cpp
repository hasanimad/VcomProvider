#include <initguid.h>
#include "common.h"
extern "C"
NTSTATUS DriverEntry(
	PDRIVER_OBJECT DriverObject,
	PUNICODE_STRING RegistryPath
)
{
	NTSTATUS status;
	WDF_DRIVER_CONFIG config;

	WDF_DRIVER_CONFIG_INIT(&config, VcomEvtDeviceAdd);

	status = WdfDriverCreate(
		DriverObject,
		RegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&config,
		WDF_NO_HANDLE
	);
	if(!NT_SUCCESS(status)) {
		KdPrint(("WdfDriverCreate failed with status 0x%08X\n", status));
		return status;
	}
	KdPrint(("DriverEntry completed successfully\n"));
	return status;
}

NTSTATUS VcomEvtDeviceAdd(
	_In_ WDFDRIVER Driver,
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT pDeviceContext;

	status = DeviceCreate(Driver, DeviceInit, &pDeviceContext);
	if(!NT_SUCCESS(status)) {
		KdPrint(("DeviceCreate failed with status 0x%08X\n", status));
		return status;
	}
	status = DeviceConfigure(pDeviceContext);
	if(!NT_SUCCESS(status)) {
		KdPrint(("DeviceConfigure failed with status 0x%08X\n", status));
		return status;
	}
	return status;
}