#ifndef _PUBLIC_H_
#define _PUBLIC_H_
#pragma once
/*++

Module Name:
    public.h

Abstract:
    Contains shared definitions for the VCOM virtual serial device.
    This file is included by both kernel-mode and user-mode components.

Environment:
    Kernel-mode + User-mode

--*/

//
// ========================================
// Section 1: Serial IOCTL compatibility
// ========================================
//
// Include the Microsoft serial.h from the sample so we have
// all IOCTL_SERIAL_* codes and related structures for compatibility.
//

#include "serial.h"  // Microsoft serial definitions


//
// ========================================
// Section 2: VCOM-specific definitions
// ========================================
//

// VCOM device symbolic link name
#define VCOM_DEVICE_NAME       L"\\Device\\VCOM"
#define VCOM_SYMBOLIC_NAME     L"\\DosDevices\\VCOM"
#define VCOM_USER_SYMBOLIC     L"\\\\.\\VCOM"

// Custom device type for VCOM
#define FILE_DEVICE_VCOM 0x8000

// Custom IOCTL function codes
#define IOCTL_VCOM_GET_OUTGOING   CTL_CODE(FILE_DEVICE_VCOM, 0x801, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define IOCTL_VCOM_PUSH_INCOMING  CTL_CODE(FILE_DEVICE_VCOM, 0x802, METHOD_IN_DIRECT,  FILE_ANY_ACCESS)
#define IOCTL_VCOM_SET_CONFIG     CTL_CODE(FILE_DEVICE_VCOM, 0x803, METHOD_BUFFERED,   FILE_ANY_ACCESS)
#define IOCTL_VCOM_GET_CONFIG     CTL_CODE(FILE_DEVICE_VCOM, 0x804, METHOD_BUFFERED,   FILE_ANY_ACCESS)
#define IOCTL_VCOM_START          CTL_CODE(FILE_DEVICE_VCOM, 0x805, METHOD_BUFFERED,   FILE_ANY_ACCESS)
#define IOCTL_VCOM_STOP           CTL_CODE(FILE_DEVICE_VCOM, 0x806, METHOD_BUFFERED,   FILE_ANY_ACCESS)

// Network transport types
typedef enum _VCOM_TRANSPORT_TYPE {
    VcomTransportTcp = 0,
    VcomTransportUdp = 1
} VCOM_TRANSPORT_TYPE;

// Configuration struct for network backend
typedef struct _VCOM_CONFIG {
    VCOM_TRANSPORT_TYPE TransportType;
    CHAR                 RemoteAddress[64];  // IPv4 or IPv6 string
    USHORT               RemotePort;
    USHORT               LocalPort;
    BOOLEAN              UseTls;
    UCHAR                Reserved[3];        // Padding for alignment
} VCOM_CONFIG, * PVCOM_CONFIG;

#endif // _PUBLIC_H_
