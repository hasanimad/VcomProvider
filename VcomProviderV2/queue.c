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

    // ================================
    // 1) Create the default parallel queue
    // ================================
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential);

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

    // Initialize small state
    queueContext->CommandMatchState = COMMAND_MATCH_STATE_IDLE;
    queueContext->ConnectCommand = FALSE;
    queueContext->IgnoreNextChar = FALSE;
    queueContext->ConnectionStateChanged = FALSE;
    queueContext->CurrentlyConnected = FALSE;

    // ================================
    // 2) Manual queue for pending reads (IRP_MJ_READ)
    // ================================
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

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

    // ================================
    // 3) Manual queue for IOCTL_SERIAL_WAIT_ON_MASK
    // ================================
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue);

    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR,
            "Error: WdfIoQueueCreate WaitMaskQueue failed 0x%x", status);
        return status;
    }

    queueContext->WaitMaskQueue = queue;

    // ================================
    // 4) Manual queue for pending IOCTL_VCOM_GET_OUTGOING
    // ================================
    WDF_IO_QUEUE_CONFIG_INIT(
        &queueConfig,
        WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfTrue; // match other manual queues

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

    // ================================
    // 5) Create spinlocks for each ring
    // ================================
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

    // ================================
    // 6) Allocate nonpaged backing storage via KMDF and init rings
    // ================================
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
        SERIAL_TIMEOUTS timeoutValues = { 0 };
        status = RequestCopyFromBuffer(Request, (void*)&timeoutValues, sizeof(timeoutValues));
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
        WDFREQUEST savedRequest;
        status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &savedRequest);
        if (NT_SUCCESS(status)) {
            WdfRequestComplete(savedRequest, STATUS_UNSUCCESSFUL);
        }
        status = WdfRequestForwardToIoQueue(Request, queueContext->WaitMaskQueue);
        if (!NT_SUCCESS(status)) {
            Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestForwardToIoQueue failed 0x%x", status);
            WdfRequestComplete(Request, status);
        }
        return; // don't complete here
    }

    case IOCTL_SERIAL_SET_WAIT_MASK:
    {
        WDFREQUEST savedRequest;
        status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &savedRequest);
        if (NT_SUCCESS(status)) {
            ULONG eventMask = 0;
            status = RequestCopyFromBuffer(savedRequest, &eventMask, sizeof(eventMask));
            WdfRequestComplete(savedRequest, status);
        }
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
    WDFREQUEST              savedRequest;
    size_t                  availableData = 0;

    Trace(TRACE_LEVEL_INFO, "EvtIoWrite 0x%p", Request);

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestRetrieveInputMemory failed 0x%x", status);
        return;
    }

    // Funnel bytes into the OUTGOING ring (to be drained by user-mode via IOCTL_VCOM_GET_OUTGOING)
    status = QueueProcessWriteBytes(
        queueContext,
        (PUCHAR)WdfMemoryGetBuffer(memory, NULL),
        Length);
    if (!NT_SUCCESS(status)) {
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
    for (;;) {
        status = WdfIoQueueRetrieveNextRequest(queueContext->OutgoingQueue, &savedRequest);
        if (!NT_SUCCESS(status)) {
            break; // no more pending
        }
        status = WdfRequestForwardToIoQueue(savedRequest, Queue);
        if (!NT_SUCCESS(status)) {
            Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestForwardToIoQueue failed 0x%x", status);
            WdfRequestComplete(savedRequest, status);
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
        Trace(TRACE_LEVEL_ERROR, "Error: WdfRequestForwardToIoQueue failed 0x%x", status);
        WdfRequestComplete(Request, status);
    }
}


NTSTATUS
QueueProcessWriteBytes(
    _In_  PQUEUE_CONTEXT    QueueContext,
    _In_reads_bytes_(Length) PUCHAR Characters,
    _In_  size_t            Length
)
/*++
This is still the sample's parser; we now write bytes into the OUTGOING ring
so user-mode can drain them. We'll keep the old AT/OK/CONNECT injection for now
(to be removed later).
--*/
{
    NTSTATUS                status = STATUS_SUCCESS;
    UCHAR                   currentCharacter;
    UCHAR                   connectString[] = "\r\nCONNECT\r\n";
    UCHAR                   connectStringCch = ARRAY_SIZE(connectString) - 1;
    UCHAR                   okString[] = "\r\nOK\r\n";
    UCHAR                   okStringCch = ARRAY_SIZE(okString) - 1;

    while (Length != 0) {
        currentCharacter = *(Characters++);
        Length--;
        if (currentCharacter == '\0') {
            continue;
        }

        WdfSpinLockAcquire(QueueContext->RingBufferToUserModeLock);
        status = RingBufferWrite(&QueueContext->RingBufferToUserMode,
            &currentCharacter,
            sizeof(currentCharacter));
        WdfSpinLockRelease(QueueContext->RingBufferToUserModeLock);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        switch (QueueContext->CommandMatchState) {
        case COMMAND_MATCH_STATE_IDLE:
            if ((currentCharacter == 'a') || (currentCharacter == 'A')) {
                QueueContext->CommandMatchState = COMMAND_MATCH_STATE_GOT_A;
                QueueContext->ConnectCommand = FALSE;
                QueueContext->IgnoreNextChar = FALSE;
            }
            break;

        case COMMAND_MATCH_STATE_GOT_A:
            if ((currentCharacter == 't') || (currentCharacter == 'T')) {
                QueueContext->CommandMatchState = COMMAND_MATCH_STATE_GOT_T;
            }
            else {
                QueueContext->CommandMatchState = COMMAND_MATCH_STATE_IDLE;
            }
            break;

        case COMMAND_MATCH_STATE_GOT_T:
            if (!QueueContext->IgnoreNextChar) {
                if ((currentCharacter == 'A') || (currentCharacter == 'a')) {
                    QueueContext->ConnectCommand = TRUE;
                }
                if ((currentCharacter == 'D') || (currentCharacter == 'd')) {
                    QueueContext->ConnectCommand = TRUE;
                }
            }
            QueueContext->IgnoreNextChar = TRUE;

            if (currentCharacter == '\r') {
                QueueContext->CommandMatchState = COMMAND_MATCH_STATE_IDLE;
                if (QueueContext->ConnectCommand) {
                    WdfSpinLockAcquire(QueueContext->RingBufferToUserModeLock);
                    status = RingBufferWrite(&QueueContext->RingBufferToUserMode,
                        connectString,
                        connectStringCch);
                    WdfSpinLockRelease(QueueContext->RingBufferToUserModeLock);
                    if (!NT_SUCCESS(status)) return status;
                    QueueContext->CurrentlyConnected = TRUE;
                    QueueContext->ConnectionStateChanged = TRUE;
                }
                else {
                    WdfSpinLockAcquire(QueueContext->RingBufferToUserModeLock);
                    status = RingBufferWrite(&QueueContext->RingBufferToUserMode,
                        okString,
                        okStringCch);
                    WdfSpinLockRelease(QueueContext->RingBufferToUserModeLock);
                    if (!NT_SUCCESS(status)) return status;
                }
            }
            break;
        default:
            break;
        }
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