#pragma once
#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#endif
#include <Ntstrsafe.h>
#include <wdf.h>


#include "public.h"
#include "driver.h"
#include "device.h"
#include "ringbuffer.h"
#include "queue.h"



//
// Tracing and Assert
//

#define Trace(level, _fmt_, ...)                    \
    DbgPrintEx(DPFLTR_DEFAULT_ID, level,            \
                _fmt_ "\n", __VA_ARGS__)

#define TRACE_LEVEL_ERROR   DPFLTR_ERROR_LEVEL
#define TRACE_LEVEL_INFO    DPFLTR_INFO_LEVEL

#ifndef ASSERT
#define ASSERT(exp) {                               \
    if (!(exp)) {                                   \
        RtlAssert(#exp, __FILE__, __LINE__, NULL);  \
    }                                               \
}
#endif