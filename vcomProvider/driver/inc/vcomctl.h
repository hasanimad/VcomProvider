#ifndef _VCOMCTL_H_
#define _VCOMCTL_H_
#pragma once

#include <ntddk.h>

#ifndef VCOM_CTL_NOGUID
#ifdef __cplusplus
extern "C" {
#endif
	extern const GUID GUID_DEVINTERFACE_VCOM_BACKEND;
#ifdef __cplusplus
}
#endif
#endif /* VCOM_CTL_NOGUID */

#ifndef FILE_DEVICE_VCOM
#define FILE_DEVICE_VCOM 0x8023
#endif

#define VCOM_MAX_IO_TRANSFER_BYTES  (64 * 1024) /* 64 KiB */

#define VCOM_CTL_GET_VERSION		CTL_CODE(FILE_DEVICE_VCOM, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VCOM_CTL_REGISTER_BACKEND	CTL_CODE(FILE_DEVICE_VCOM, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VCOM_CTL_GET_APP_WRITE		CTL_CODE(FILE_DEVICE_VCOM, 0x802, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define VCOM_CTL_PUT_APP_READ		CTL_CODE(FILE_DEVICE_VCOM, 0x803, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define VCOM_CTL_SET_SETTINGS		CTL_CODE(FILE_DEVICE_VCOM, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VCOM_CTL_GET_STATS 			CTL_CODE(FILE_DEVICE_VCOM, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct _VCOM_CTL_SETTINGS {
	ULONG BaudRate;
	UCHAR Parity;		// 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
	UCHAR StopBits;		// 0: 1 Stop Bit, 1: 1.5 Stop Bits, 2: 2 Stop Bits
	UCHAR ByteSize;		// 5 to 8 bits
	UCHAR FlowControl;	// 0: None, 1: XON/XOFF, 2: RTS/CTS
} VCOM_CTL_SETTINGS, * PVCOM_CTL_SETTINGS;

struct VCOM_STATS {
	unsigned __int64 NetToAppBytes; // Bytes sent from network to driver/application
	unsigned __int64 AppToNetBytes; // Bytes sent from driver/application to network
};

typedef struct _VCOM_VERSION {
	USHORT Majorr;
	USHORT Minor;
	USHORT Patch;
	USHORT Reserved;
} VCOM_VERSION, * PVCOM_VERSION;

#pragma pack(pop)

static_assert(sizeof(VCOM_CTL_SETTINGS) == 8, "VCOM_CTL_SETTINGS size mismatch");
static_assert(sizeof(VCOM_STATS) == 16, "VCOM_STATS size mismatch");
static_assert(sizeof(VCOM_VERSION) == 8, "VCOM_VERSION size mismatch");



#endif // _VCOMCTL_H_
