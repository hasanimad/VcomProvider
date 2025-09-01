#pragma once

#define DATA_BUFFER_SIZE 1024

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define MAXULONG 0xffffffff

typedef struct _QUEUE_CONTEXT {
    // ===== Outgoing: App -> Service (drained by IOCTL_VCOM_GET_OUTGOING)
    RING_BUFFER     RingBufferToUserMode;
    WDFSPINLOCK     RingBufferToUserModeLock;

    // Backing storage owned by KMDF (nonpaged)
    PUCHAR          ToUserBuffer;
    WDFMEMORY       ToUserMem;
    SIZE_T          ToUserCapacity;

    // ===== Incoming: Service -> App (filled by IOCTL_VCOM_PUSH_INCOMING)
    RING_BUFFER     RingBufferFromNetwork;
    WDFSPINLOCK     RingBufferFromNetworkLock;

    // Backing storage owned by KMDF (nonpaged)
    PUCHAR          FromNetBuffer;
    WDFMEMORY       FromNetMem;
    SIZE_T          FromNetCapacity;

    // Manual queue for blocking GET_OUTGOING IOCTLs
    WDFQUEUE        OutgoingQueue;

    // Standard queues
    WDFQUEUE        Queue;           // Default parallel queue
    WDFQUEUE        ReadQueue;       // Manual queue for pending reads


    PDEVICE_CONTEXT DeviceContext;   // Back-reference to device context

} QUEUE_CONTEXT, * PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, GetQueueContext);

// Queue event handlers
EVT_WDF_IO_QUEUE_IO_READ           EvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE          EvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE EvtIoCanceledOnQueue;

// Queue management
NTSTATUS QueueCreate(_In_ PDEVICE_CONTEXT DeviceContext);

// Data processing helpers
NTSTATUS QueueProcessWriteBytes(
    _In_  PQUEUE_CONTEXT QueueContext,
    _In_reads_bytes_(Length) PUCHAR Characters,
    _In_  size_t Length
);

NTSTATUS QueueProcessGetLineControl(
    _In_  PQUEUE_CONTEXT QueueContext,
    _In_  WDFREQUEST     Request
);

NTSTATUS QueueProcessSetLineControl(
    _In_  PQUEUE_CONTEXT QueueContext,
    _In_  WDFREQUEST     Request
);

// Request buffer helpers
NTSTATUS RequestCopyFromBuffer(
    _In_ WDFREQUEST Request,
    _In_ PVOID      SourceBuffer,
    _In_ size_t     NumBytesToCopyFrom
);

NTSTATUS RequestCopyToBuffer(
    _In_ WDFREQUEST Request,
    _In_ PVOID      DestinationBuffer,
    _In_ size_t     NumBytesToCopyTo
);