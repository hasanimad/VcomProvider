#include "common.h"

NTSTATUS
QueueCreate(
    _In_  PDEVICE_CONTEXT   DeviceContext
)
{
    NTSTATUS                status;
    WDFDEVICE               device = DeviceContext->Device;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    WDF_OBJECT_ATTRIBUTES   queueAttributes;
    WDFQUEUE                queue;
    PQUEUE_CONTEXT          queueContext;

    
    // 1) Create the default parallel queue
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel);

    queueConfig.EvtIoRead = EvtIoRead;
    queueConfig.EvtIoWrite = EvtIoWrite;
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &queueAttributes,
        QUEUE_CONTEXT);

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        &queueAttributes,
        &queue);

    if (!NT_SUCCESS(status)) {
        KdPrint(("Error: WdfIoQueueCreate failed 0x%x\n", status));
        return status;
    }

    queueContext = GetQueueContext(queue);
    queueContext->Queue = queue;
    queueContext->DeviceContext = DeviceContext;
    queueContext->DeviceContext->IoQueue = queue; // let cleanup reach our manual queues & rings

    // 2) Manual queue for pending reads (IRP_MJ_READ)
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);
	queueConfig.PowerManaged = WdfTrue;
    queueConfig.EvtIoCanceledOnQueue = EvtIoCanceledOnQueue;
    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue);

    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfIoQueueCreate ReadQueue failed 0x%x", status);
        return status;
    }

    queueContext->ReadQueue = queue;

    // 3) Manual queue for pending IOCTL_VCOM_GET_OUTGOING
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    queueConfig.EvtIoCanceledOnQueue = EvtIoCanceledOnQueue;
    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queueContext->OutgoingQueue);

    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfIoQueueCreate OutgoingQueue failed 0x%x", status);
        return status;
    }

    // 4) Create spinlocks for each ring
    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &queueContext->RingBufferToUserModeLock);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "RingBufferToUserModeLock create failed 0x%x", status);
        return status;
    }

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &queueContext->RingBufferFromNetworkLock);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "RingBufferFromNetworkLock create failed 0x%x", status);
        return status;
    }

    // 5) Allocate nonpaged backing storage via KMDF and init rings
    WDF_OBJECT_ATTRIBUTES memAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&memAttr);
    // Tie lifetime to the default queue; device lifetime works too
    memAttr.ParentObject = queueContext->Queue;

    status = WdfMemoryCreate(&memAttr, NonPagedPoolNx, 'moVT',
        DATA_BUFFER_SIZE,
        &queueContext->ToUserMem,
        (PVOID*)&queueContext->ToUserBuffer);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate(ToUser) failed 0x%x", status);
        return status;
    }

    status = WdfMemoryCreate(&memAttr, NonPagedPoolNx, 'moVF',
        DATA_BUFFER_SIZE,
        &queueContext->FromNetMem,
        (PVOID*)&queueContext->FromNetBuffer);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate(FromNet) failed 0x%x", status);
        return status;
    }

    queueContext->ToUserCapacity = DATA_BUFFER_SIZE;
    queueContext->FromNetCapacity = DATA_BUFFER_SIZE;

    RingBufferInitialize(&queueContext->RingBufferToUserMode,
        queueContext->ToUserBuffer,
        queueContext->ToUserCapacity);

    RingBufferInitialize(&queueContext->RingBufferFromNetwork,
        queueContext->FromNetBuffer,
        queueContext->FromNetCapacity);

    return STATUS_SUCCESS;
}


NTSTATUS
RequestCopyFromBuffer(
    _In_  WDFREQUEST        Request,
    _In_  PVOID             SourceBuffer,
    _In_  size_t            NumBytesToCopyFrom
)
{
    NTSTATUS                status;
    WDFMEMORY               memory;

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfRequestRetrieveOutputMemory failed 0x%x", status);
        return status;
    }

    status = WdfMemoryCopyFromBuffer(memory, 0,
        SourceBuffer, NumBytesToCopyFrom);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfMemoryCopyFromBuffer failed 0x%x", status);
        return status;
    }

    WdfRequestSetInformation(Request, NumBytesToCopyFrom);
    return status;
}


NTSTATUS
RequestCopyToBuffer(
    _In_  WDFREQUEST        Request,
    _In_  PVOID             DestinationBuffer,
    _In_  size_t            NumBytesToCopyTo
)
{
    NTSTATUS                status;
    WDFMEMORY               memory;

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfRequestRetrieveInputMemory failed 0x%x", status);
        return status;
    }

    status = WdfMemoryCopyToBuffer(memory, 0,
        DestinationBuffer, NumBytesToCopyTo);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfMemoryCopyToBuffer failed 0x%x", status);
        return status;
    }

    return status;
}


VOID
EvtIoDeviceControl(
    _In_  WDFQUEUE          Queue,
    _In_  WDFREQUEST        Request,
    _In_  size_t            OutputBufferLength,
    _In_  size_t            InputBufferLength,
    _In_  ULONG             IoControlCode
)
{
    NTSTATUS                status = STATUS_SUCCESS;
    PQUEUE_CONTEXT          queueContext = GetQueueContext(Queue);
    PDEVICE_CONTEXT         deviceContext = queueContext->DeviceContext;
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    Trace(TRACE_LEVEL_INFO,
        "EvtIoDeviceControl 0x%x", IoControlCode);

    switch (IoControlCode)
    {
    case IOCTL_SERIAL_SET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baudRateBuffer = { 0 };
        status = RequestCopyToBuffer(Request, &baudRateBuffer, sizeof(baudRateBuffer));
        if (NT_SUCCESS(status)) {
            SetBaudRate(deviceContext, baudRateBuffer.BaudRate);
        }
        break;
    }

    case IOCTL_SERIAL_GET_BAUD_RATE:
    {
        SERIAL_BAUD_RATE baudRateBuffer = { 0 };
        baudRateBuffer.BaudRate = GetBaudRate(deviceContext);
        status = RequestCopyFromBuffer(Request, &baudRateBuffer, sizeof(baudRateBuffer));
        break;
    }

    case IOCTL_SERIAL_SET_MODEM_CONTROL:
    {
        ULONG* modemControlRegister = GetModemControlRegister(deviceContext);
        ASSERT(modemControlRegister);
        status = RequestCopyToBuffer(Request, modemControlRegister, sizeof(ULONG));
        break;
    }

    case IOCTL_SERIAL_GET_MODEM_CONTROL:
    {
        ULONG* modemControlRegister = GetModemControlRegister(deviceContext);
        ASSERT(modemControlRegister);
        status = RequestCopyFromBuffer(Request, modemControlRegister, sizeof(ULONG));
        break;
    }

    case IOCTL_SERIAL_SET_FIFO_CONTROL:
    {
        ULONG* fifoControlRegister = GetFifoControlRegisterPtr(deviceContext);
        ASSERT(fifoControlRegister);
        status = RequestCopyToBuffer(Request, fifoControlRegister, sizeof(ULONG));
        break;
    }

    case IOCTL_SERIAL_GET_LINE_CONTROL:
    {
        status = QueueProcessGetLineControl(queueContext, Request);
        break;
    }

    case IOCTL_SERIAL_SET_LINE_CONTROL:
    {
        status = QueueProcessSetLineControl(queueContext, Request);
        break;
    }

    case IOCTL_SERIAL_GET_TIMEOUTS:
    {

        status = RequestCopyFromBuffer(Request, (void*)&deviceContext->Timeouts, sizeof(deviceContext->Timeouts));
        break;
    }

    case IOCTL_SERIAL_SET_TIMEOUTS:
    {
        SERIAL_TIMEOUTS timeoutValues = { 0 };
        status = RequestCopyToBuffer(Request, (void*)&timeoutValues, sizeof(timeoutValues));
        if (NT_SUCCESS(status))
        {
            if ((timeoutValues.ReadIntervalTimeout == MAXULONG) &&
                (timeoutValues.ReadTotalTimeoutMultiplier == MAXULONG) &&
                (timeoutValues.ReadTotalTimeoutConstant == MAXULONG))
            {
                status = STATUS_INVALID_PARAMETER;
            }
        }
        if (NT_SUCCESS(status)) {
            SetTimeouts(deviceContext, timeoutValues);
        }
        break;
    }

    case IOCTL_SERIAL_WAIT_ON_MASK:
    {
        // Forward the request to our dedicated manual queue to pend it.
    // WDF will handle cleanup if the file handle is closed.
        ULONG* pMask = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &pMask, NULL);
        // Complete the request, returning 0 events.
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }

    case IOCTL_SERIAL_SET_WAIT_MASK:
    {
        // We don't track the wait mask, so we can just succeed this request.
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_SERIAL_SET_QUEUE_SIZE:
    case IOCTL_SERIAL_SET_DTR:
    case IOCTL_SERIAL_SET_RTS:
    case IOCTL_SERIAL_CLR_RTS:
    case IOCTL_SERIAL_SET_XON:
    case IOCTL_SERIAL_SET_XOFF:
    case IOCTL_SERIAL_SET_CHARS:
    case IOCTL_SERIAL_GET_CHARS:
    case IOCTL_SERIAL_GET_HANDFLOW:
    case IOCTL_SERIAL_SET_HANDFLOW:
    case IOCTL_SERIAL_RESET_DEVICE:
        status = STATUS_SUCCESS;
        break;
    case IOCTL_VCOM_GET_OUTGOING:
    {
        if (!deviceContext->Started) { status = STATUS_DEVICE_NOT_READY; break; }

        PVOID outBuf = NULL;
        size_t outLen = 0, copied = 0;

        status = WdfRequestRetrieveOutputBuffer(Request, 1, &outBuf, &outLen);
        if (!NT_SUCCESS(status)) break;

        if (outLen == 0) { WdfRequestSetInformation(Request, 0); status = STATUS_SUCCESS; break; }

        WdfSpinLockAcquire(queueContext->RingBufferToUserModeLock);
        status = RingBufferRead(&queueContext->RingBufferToUserMode,
            (BYTE*)outBuf, outLen, &copied);
        WdfSpinLockRelease(queueContext->RingBufferToUserModeLock);
        if (!NT_SUCCESS(status)) break;

        if (copied > 0) {
            WdfRequestSetInformation(Request, copied);
            status = STATUS_SUCCESS;
            break;
        }

        // No data: pend on manual OutgoingQueue
        status = WdfRequestForwardToIoQueue(Request, queueContext->OutgoingQueue);
        if (!NT_SUCCESS(status)) {
            Trace(TRACE_LEVEL_ERROR, "GET_OUTGOING forward failed (WdfRequestForwardToIoQueue:Outgoing) 0x%x", status);
            WdfRequestComplete(Request, status);
        }
        return; // don't complete here
    }
    case IOCTL_VCOM_PUSH_INCOMING:
    {
        if (!deviceContext->Started) { status = STATUS_DEVICE_NOT_READY; break; }

        WDFMEMORY inMem;
        size_t inLen = 0, wrote = 0;
        status = WdfRequestRetrieveInputMemory(Request, &inMem);
        if (!NT_SUCCESS(status)) break;

        BYTE* src = (BYTE*)WdfMemoryGetBuffer(inMem, &inLen);

        if (src && inLen) {
            WdfSpinLockAcquire(queueContext->RingBufferFromNetworkLock);
            status = RingBufferWritePartial(&queueContext->RingBufferFromNetwork, src, inLen, &wrote);
            WdfSpinLockRelease(queueContext->RingBufferFromNetworkLock);
            // STATUS_SUCCESS if fully accepted, STATUS_BUFFER_OVERFLOW if partial
            if (status == STATUS_BUFFER_OVERFLOW) status = STATUS_SUCCESS; // we return success + byte count
        }

        // Wake pending reads — re-dispatch to default queue so EvtIoRead can copy
        for (;;) {
            WDFREQUEST rd;
            NTSTATUS s2 = WdfIoQueueRetrieveNextRequest(queueContext->ReadQueue, &rd);
            if (!NT_SUCCESS(s2)) break;
            s2 = WdfRequestForwardToIoQueue(rd, queueContext->Queue);
            if (!NT_SUCCESS(s2)) {
                Trace(TRACE_LEVEL_ERROR, "Forward read after PUSH failed 0x%x", s2);
                WdfRequestComplete(rd, s2);
            }
        }

        WdfRequestSetInformation(Request, wrote);
        break;
    }
    
    case IOCTL_VCOM_START:
        
        (void)WdfIoQueueStart(queueContext->Queue);
        (void)WdfIoQueueStart(queueContext->ReadQueue);
        (void)WdfIoQueueStart(queueContext->OutgoingQueue);


        KdPrint(("VCOM: I/O Queues started.\n"));
    {
        deviceContext->Started = TRUE;
        WdfSpinLockAcquire(queueContext->RingBufferToUserModeLock);
        RingBufferReset(&queueContext->RingBufferToUserMode);
        WdfSpinLockRelease(queueContext->RingBufferToUserModeLock);

        WdfSpinLockAcquire(queueContext->RingBufferFromNetworkLock);
        RingBufferReset(&queueContext->RingBufferFromNetwork);
        WdfSpinLockRelease(queueContext->RingBufferFromNetworkLock);

        status = STATUS_SUCCESS;
        break;
    }
    case IOCTL_VCOM_STOP:
    {
        WDFREQUEST req;

        KdPrint(("VCOM: IOCTL_VCOM_STOP received. Draining queues.\n"));
        // Close the gate so new operations see device stopped
        deviceContext->Started = FALSE;

        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(queueContext->ReadQueue, &req))) {
            KdPrint(("VCOM: Completing pending read request during STOP.\n"));
            WdfRequestComplete(req, STATUS_CANCELLED);
        }

        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(queueContext->OutgoingQueue, &req))) {
            KdPrint(("VCOM: Completing pending outgoing request during STOP.\n"));
            WdfRequestComplete(req, STATUS_CANCELLED);
        }

        status = STATUS_SUCCESS; 
        KdPrint(("VCOM: IOCTL_VCOM_STOP Finished.\n"));
        break;
    }
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    WdfRequestComplete(Request, status);
}


VOID
EvtIoWrite(
    _In_  WDFQUEUE          Queue,
    _In_  WDFREQUEST        Request,
    _In_  size_t            Length
)
{
    NTSTATUS                status;
    PQUEUE_CONTEXT          queueContext = GetQueueContext(Queue);
    WDFMEMORY               memory;
    size_t                  availableData = 0;

    Trace(TRACE_LEVEL_INFO, "EvtIoWrite 0x%p", Request);
    
    if (!queueContext->DeviceContext->Started) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;

    }

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestRetrieveInputMemory failed 0x%x", status);
        WdfRequestComplete(Request, status);
        return;
    }

    // Funnel bytes into the OUTGOING ring (to be drained by user-mode via IOCTL_VCOM_GET_OUTGOING)
    status = QueueProcessWriteBytes(
        queueContext,
        (PUCHAR)WdfMemoryGetBuffer(memory, NULL),
        Length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfRequestCompleteWithInformation(Request, status, Length);

    // Check how much is available to drain by any pending GET_OUTGOING IOCTL
    WdfSpinLockAcquire(queueContext->RingBufferToUserModeLock);
    RingBufferGetAvailableData(&queueContext->RingBufferToUserMode, &availableData);
    WdfSpinLockRelease(queueContext->RingBufferToUserModeLock);

    if (availableData == 0) {
        return;
    }

    // Wake any pending GET_OUTGOING requests by re-dispatching them
    // Satisfy pending GET_OUTGOING
    for (;;) {
        WDFREQUEST      getOutgoingRequest;
        NTSTATUS        s;

        s = WdfIoQueueRetrieveNextRequest(queueContext->OutgoingQueue, &getOutgoingRequest);
        if (!NT_SUCCESS(s)) {
            // No more pending requests to fulfill
            break;
        }

        // We have a pending IOCTL. Let's try to complete it.
        PVOID   outputBuffer = NULL;
        size_t  outputBufferLength = 0;
        size_t  bytesCopied = 0;

        s = WdfRequestRetrieveOutputBuffer(getOutgoingRequest, 1, &outputBuffer, &outputBufferLength);
        if (!NT_SUCCESS(s)) {
            // Failed to get buffer, complete with an error.
            WdfRequestComplete(getOutgoingRequest, s);
            continue;
        }

        // Read from the ring buffer into the IOCTL's buffer
        WdfSpinLockAcquire(queueContext->RingBufferToUserModeLock);
        RingBufferRead(
            &queueContext->RingBufferToUserMode,
            (BYTE*)outputBuffer,
            outputBufferLength,
            &bytesCopied
        );
        WdfSpinLockRelease(queueContext->RingBufferToUserModeLock);

        if (bytesCopied > 0) {
            // We read some data, complete the request successfully.
            WdfRequestCompleteWithInformation(getOutgoingRequest, STATUS_SUCCESS, bytesCopied);
        }
        else {
            // Race condition: data was drained by another thread between our check
            // and retrieving the request. Re-queue it.
            s = WdfRequestForwardToIoQueue(getOutgoingRequest, queueContext->OutgoingQueue);
            if (!NT_SUCCESS(s)) {
                Trace(TRACE_LEVEL_ERROR, "Forward read after PUSH failed 0x%x", s);
                // If invalid state, complete canceled to avoid leaks
                if (s == STATUS_WDF_REQUEST_INVALID_STATE || s == STATUS_CANCELLED) {
                    WdfRequestComplete(getOutgoingRequest, STATUS_CANCELLED);
                }
                else {
                    WdfRequestComplete(getOutgoingRequest, s);
                }
            }
        }
    }
}


VOID
EvtIoRead(
    _In_  WDFQUEUE          Queue,
    _In_  WDFREQUEST        Request,
    _In_  size_t            Length
)
{
    NTSTATUS                status;
    PQUEUE_CONTEXT          queueContext = GetQueueContext(Queue);
    WDFMEMORY               memory;
    size_t                  bytesCopied = 0;

    Trace(TRACE_LEVEL_INFO, "EvtIoRead 0x%p", Request);

    if (!queueContext->DeviceContext->Started) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
        
    }

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestRetrieveOutputMemory failed 0x%x", status);
        WdfRequestComplete(Request, status);
        return;
    }

    // Read from the INCOMING ring (filled via IOCTL_VCOM_PUSH_INCOMING)
    WdfSpinLockAcquire(queueContext->RingBufferFromNetworkLock);
    status = RingBufferRead(&queueContext->RingBufferFromNetwork,
        (BYTE*)WdfMemoryGetBuffer(memory, NULL),
        Length,
        &bytesCopied);
    WdfSpinLockRelease(queueContext->RingBufferFromNetworkLock);

    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    if (bytesCopied > 0) {
        WdfRequestCompleteWithInformation(Request, status, bytesCopied);
        return;
    }

    // No data: pend this read until PUSH_INCOMING arrives
    status = WdfRequestForwardToIoQueue(Request, queueContext->ReadQueue);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestForwardToIoQueue(Read Queue) failed 0x%x", status);
        WdfRequestComplete(Request, status);
    }
}

VOID
EvtIoCanceledOnQueue(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request)
{
    UNREFERENCED_PARAMETER(Queue);
    WdfRequestComplete(Request, STATUS_CANCELLED);
}

NTSTATUS
QueueProcessWriteBytes(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_reads_bytes_(Length) PUCHAR Characters,
    _In_  size_t            Length
)
/*++
Routine Description:

    This function takes the data from an application's write request and
    places it into the outgoing ring buffer. This buffer is then drained
    by the user-mode client application via the IOCTL_VCOM_GET_OUTGOING
    request, effectively creating a pipe from the virtual serial port to
    the client application.

Arguments:

    QueueContext - A pointer to the queue's context space.

    Characters - A pointer to the buffer containing the data to be written.

    Length - The number of bytes to write from the Characters buffer.

Return Value:

    STATUS_SUCCESS if the data was written successfully.
    An appropriate NTSTATUS error code otherwise.

--*/
{
    NTSTATUS status;
    size_t   bytesWritten = 0;

    // If there's nothing to write, we're done.
    if (Length == 0) {
        return STATUS_SUCCESS;
    }

    // Acquire the lock to ensure exclusive access to the ring buffer.
    WdfSpinLockAcquire(QueueContext->RingBufferToUserModeLock);

    // Write the entire block of data from the application to the outgoing ring buffer.
    // We use RingBufferWritePartial here as it will accept as much data as it can
    // without overflowing, which is safer. The number of bytes actually written
    // is returned in bytesWritten.
    status = RingBufferWritePartial(
        &QueueContext->RingBufferToUserMode,
        Characters,
        Length,
        &bytesWritten
    );

    // Release the lock.
    WdfSpinLockRelease(QueueContext->RingBufferToUserModeLock);

    // For this driver's logic, even a partial write is not an error condition
    // from the perspective of the calling function (EvtIoWrite), so we
    // normalize the status to success. The EvtIoWrite function will complete
    // the original request with the actual number of bytes written.
    if (status == STATUS_BUFFER_OVERFLOW) {
        status = STATUS_SUCCESS;
    }

    return status;
}


NTSTATUS
QueueProcessGetLineControl(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_  WDFREQUEST        Request
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         deviceContext;
    SERIAL_LINE_CONTROL     lineControl = { 0 };
    ULONG                   lineControlSnapshot;
    ULONG* lineControlRegister;

    deviceContext = QueueContext->DeviceContext;
    lineControlRegister = GetLineControlRegisterPtr(deviceContext);

    ASSERT(lineControlRegister);

    lineControlSnapshot = ReadNoFence((LONG*)lineControlRegister);

    if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_5_DATA) { lineControl.WordLength = 5; }
    else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_6_DATA) { lineControl.WordLength = 6; }
    else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_7_DATA) { lineControl.WordLength = 7; }
    else if ((lineControlSnapshot & SERIAL_DATA_MASK) == SERIAL_8_DATA) { lineControl.WordLength = 8; }

    if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_NONE_PARITY) { lineControl.Parity = NO_PARITY; }
    else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_ODD_PARITY) { lineControl.Parity = ODD_PARITY; }
    else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_EVEN_PARITY) { lineControl.Parity = EVEN_PARITY; }
    else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_MARK_PARITY) { lineControl.Parity = MARK_PARITY; }
    else if ((lineControlSnapshot & SERIAL_PARITY_MASK) == SERIAL_SPACE_PARITY) { lineControl.Parity = SPACE_PARITY; }

    if (lineControlSnapshot & SERIAL_2_STOP) {
        if (lineControl.WordLength == 5) { lineControl.StopBits = STOP_BITS_1_5; }
        else { lineControl.StopBits = STOP_BITS_2; }
    }
    else {
        lineControl.StopBits = STOP_BIT_1;
    }

    status = RequestCopyFromBuffer(Request, (void*)&lineControl, sizeof(lineControl));
    return status;
}


NTSTATUS
QueueProcessSetLineControl(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_  WDFREQUEST        Request
)
{
    NTSTATUS                status;
    PDEVICE_CONTEXT         deviceContext;
    SERIAL_LINE_CONTROL     lineControl = { 0 };
    ULONG* lineControlRegister;
    UCHAR                   lineControlData = 0;
    UCHAR                   lineControlStop = 0;
    UCHAR                   lineControlParity = 0;
    ULONG                   lineControlSnapshot;
    ULONG                   lineControlNew;
    ULONG                   lineControlPrevious;
    ULONG                   i;

    deviceContext = QueueContext->DeviceContext;
    lineControlRegister = GetLineControlRegisterPtr(deviceContext);

    ASSERT(lineControlRegister);

    status = RequestCopyToBuffer(Request, (void*)&lineControl, sizeof(lineControl));

    if (NT_SUCCESS(status)) {
        switch (lineControl.WordLength)
        {
        case 5: lineControlData = SERIAL_5_DATA; SetValidDataMask(deviceContext, 0x1f); break;
        case 6: lineControlData = SERIAL_6_DATA; SetValidDataMask(deviceContext, 0x3f); break;
        case 7: lineControlData = SERIAL_7_DATA; SetValidDataMask(deviceContext, 0x7f); break;
        case 8: lineControlData = SERIAL_8_DATA; SetValidDataMask(deviceContext, 0xff); break;
        default: status = STATUS_INVALID_PARAMETER; break;
        }
    }

    if (NT_SUCCESS(status)) {
        switch (lineControl.StopBits)
        {
        case STOP_BIT_1:  lineControlStop = SERIAL_1_STOP; break;
        case STOP_BITS_1_5:
            if (lineControlData != SERIAL_5_DATA) { status = STATUS_INVALID_PARAMETER; break; }
            lineControlStop = SERIAL_1_5_STOP; break;
        case STOP_BITS_2:
            if (lineControlData == SERIAL_5_DATA) { status = STATUS_INVALID_PARAMETER; break; }
            lineControlStop = SERIAL_2_STOP; break;
        default: status = STATUS_INVALID_PARAMETER; break;
        }
    }

    if (NT_SUCCESS(status)) {
        switch (lineControl.Parity)
        {
        case NO_PARITY:   lineControlParity = SERIAL_NONE_PARITY;  break;
        case EVEN_PARITY: lineControlParity = SERIAL_EVEN_PARITY;  break;
        case ODD_PARITY:  lineControlParity = SERIAL_ODD_PARITY;   break;
        case SPACE_PARITY:lineControlParity = SERIAL_SPACE_PARITY; break;
        case MARK_PARITY: lineControlParity = SERIAL_MARK_PARITY;  break;
        default: status = STATUS_INVALID_PARAMETER; break;
        }
    }

    i = 0;
    do {
        i++;
        if ((i & 0xf) == 0) {
#ifdef _KERNEL_MODE
            LARGE_INTEGER   interval; interval.QuadPart = 0; KeDelayExecutionThread(UserMode, FALSE, &interval);
#else
            SwitchToThread();
#endif
        }

        lineControlSnapshot = ReadNoFence((LONG*)lineControlRegister);
        lineControlNew = (lineControlSnapshot & SERIAL_LCR_BREAK) |
            (lineControlData | lineControlParity | lineControlStop);
        lineControlPrevious = InterlockedCompareExchange(
            (LONG*)lineControlRegister,
            lineControlNew,
            lineControlSnapshot);

    } while (lineControlPrevious != lineControlSnapshot);

    return status;
}