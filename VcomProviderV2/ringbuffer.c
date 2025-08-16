// ==========================
// ringbuffer.cpp (updated)
// ==========================
/*++

Copyright (c) Microsoft Corporation, All Rights Reserved

Module Name:

    RingBuffer.c

Abstract:

    DISPATCH_LEVEL-safe ring buffer with optional partial writes

Environment:

    Kernel-mode

--*/

#include "common.h"

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
RingBufferInitialize(
    _Inout_ PRING_BUFFER      Self,
    _In_reads_bytes_(BufferSize)
    BYTE* Buffer,
    _In_  size_t              BufferSize
)
{
    Self->Size = BufferSize;
    Self->Base = Buffer;
    Self->End = Buffer + BufferSize;
    Self->Head = Buffer;
    Self->Tail = Buffer;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
RingBufferGetAvailableSpace(
    _In_  PRING_BUFFER      Self,
    _Out_ size_t* AvailableSpace
)
{
    BYTE* headSnapshot = NULL;
    BYTE* tailSnapshot = NULL;
    BYTE* tailPlusOne = NULL;

    ASSERT(AvailableSpace);

    headSnapshot = Self->Head;
    tailSnapshot = Self->Tail;

    // Distinguish empty vs full by keeping one byte always unused.
    tailPlusOne = ((tailSnapshot + 1) == Self->End) ? Self->Base : (tailSnapshot + 1);

    if (tailPlusOne == headSnapshot) {
        *AvailableSpace = 0; // full
    }
    else if (tailSnapshot == headSnapshot) {
        *AvailableSpace = Self->Size - 1; // empty
    }
    else if (tailSnapshot > headSnapshot) {
        *AvailableSpace = Self->Size - (tailSnapshot - headSnapshot) - 1; // contiguous
    }
    else {
        *AvailableSpace = (headSnapshot - tailSnapshot) - 1; // wrapped
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
RingBufferGetAvailableData(
    _In_  PRING_BUFFER      Self,
    _Out_ size_t* AvailableData
)
{
    size_t availableSpace = 0;
    ASSERT(AvailableData);
    RingBufferGetAvailableSpace(Self, &availableSpace);
    *AvailableData = Self->Size - availableSpace - 1; // one byte always unused
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RingBufferWritePartial(
    _Inout_ PRING_BUFFER      Self,
    _In_reads_bytes_(DataSize)
    const BYTE* Data,
    _In_  size_t              DataSize,
    _Out_ size_t* BytesWritten
)
{
    size_t availableSpace;
    size_t bytesToCopy;
    size_t spaceFromCurrToEnd;

    if (BytesWritten) *BytesWritten = 0;

    ASSERT(Data && (0 != DataSize));

    if ((Self == NULL) || (Self->Tail >= Self->End) || (Self->Base == NULL)) {
        return STATUS_INTERNAL_ERROR;
    }

    RingBufferGetAvailableSpace(Self, &availableSpace);

    bytesToCopy = (availableSpace < DataSize) ? availableSpace : DataSize;

    if (bytesToCopy) {
        if ((Self->Tail + bytesToCopy) > Self->End) {
            // Two-step copy (wraps)
            spaceFromCurrToEnd = (size_t)(Self->End - Self->Tail);
            RtlCopyMemory(Self->Tail, Data, spaceFromCurrToEnd);
            Data += spaceFromCurrToEnd;
            bytesToCopy -= spaceFromCurrToEnd;
            RtlCopyMemory(Self->Base, Data, bytesToCopy);
            Self->Tail = Self->Base + bytesToCopy; // wrapped position
        }
        else {
            // Single-step copy
            RtlCopyMemory(Self->Tail, Data, bytesToCopy);
            Self->Tail += bytesToCopy;
            if (Self->Tail == Self->End) {
                Self->Tail = Self->Base; // wrap exactly at end
            }
        }
        ASSERT(Self->Tail < Self->End);
    }

    if (BytesWritten) *BytesWritten = bytesToCopy + 0; // number accepted
    return (bytesToCopy == DataSize) ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
RingBufferRead(
    _Inout_ PRING_BUFFER      Self,
    _Out_writes_bytes_to_(DataSize, *BytesCopied)
    BYTE* Data,
    _In_  size_t              DataSize,
    _Out_ size_t* BytesCopied
)
{
    size_t availableData;
    size_t dataFromCurrToEnd;

    ASSERT(Data && (DataSize != 0));

    if ((Self == NULL) || (Self->Head >= Self->End) || (Self->Base == NULL)) {
        return STATUS_INTERNAL_ERROR;
    }

    RingBufferGetAvailableData(Self, &availableData);

    if (availableData == 0) {
        if (BytesCopied) *BytesCopied = 0;
        return STATUS_SUCCESS;
    }

    if (DataSize > availableData) {
        DataSize = availableData;
    }

    if (BytesCopied) *BytesCopied = DataSize;

    if ((Self->Head + DataSize) > Self->End) {
        // Two-step copy (wraps)
        dataFromCurrToEnd = (size_t)(Self->End - Self->Head);
        RtlCopyMemory(Data, Self->Head, dataFromCurrToEnd);
        Data += dataFromCurrToEnd;
        DataSize -= dataFromCurrToEnd;
        RtlCopyMemory(Data, Self->Base, DataSize);
        Self->Head = Self->Base + DataSize;
    }
    else {
        // Single-step copy
        RtlCopyMemory(Data, Self->Head, DataSize);
        Self->Head += DataSize;
        if (Self->Head == Self->End) {
            Self->Head = Self->Base;
        }
    }

    ASSERT(Self->Head < Self->End);
    return STATUS_SUCCESS;
}
