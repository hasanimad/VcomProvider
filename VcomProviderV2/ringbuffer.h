// ==========================
// ringbuffer.h (updated)
// ==========================
/*++

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    Ringbuffer.h

--*/

#pragma once


#ifdef __cplusplus
extern "C" {
#endif

    typedef struct _RING_BUFFER
    {
        // Total storage size in bytes (includes one sentinel byte left unused)
        size_t Size;

        // Base address of the backing storage (nonpaged)
        BYTE* Base;

        // One byte past the end of the buffer (Base + Size)
        BYTE* End;

        // Read cursor
        BYTE* Head;

        // Write cursor
        BYTE* Tail;

    } RING_BUFFER, * PRING_BUFFER;

    _IRQL_requires_max_(DISPATCH_LEVEL)
        VOID
        RingBufferInitialize(
            _Inout_ PRING_BUFFER      Self,
            _In_reads_bytes_(BufferSize)
            _When_(BufferSize > 0, _Notnull_)
            BYTE* Buffer,
            _In_  size_t              BufferSize
        );

    // Legacy behavior: write as much as fits (possibly truncated) and return STATUS_SUCCESS.
    _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS
        RingBufferWrite(
            _Inout_ PRING_BUFFER      Self,
            _In_reads_bytes_(DataSize)
            const BYTE* Data,
            _In_  size_t              DataSize
        );

    // New: report how many bytes were accepted; returns STATUS_SUCCESS if fully written,
    // STATUS_BUFFER_OVERFLOW if truncated (BytesWritten < DataSize).
    _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS
        RingBufferWritePartial(
            _Inout_ PRING_BUFFER      Self,
            _In_reads_bytes_(DataSize)
            const BYTE* Data,
            _In_  size_t              DataSize,
            _Out_ size_t* BytesWritten
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
        NTSTATUS
        RingBufferRead(
            _Inout_ PRING_BUFFER      Self,
            _Out_writes_bytes_to_(DataSize, *BytesCopied)
            _When_(DataSize > 0, _Notnull_)
            BYTE* Data,
            _In_  size_t              DataSize,
            _Out_ size_t* BytesCopied
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
        VOID
        RingBufferGetAvailableSpace(
            _In_  PRING_BUFFER      Self,
            _Out_ size_t* AvailableSpace
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
        VOID
        RingBufferGetAvailableData(
            _In_  PRING_BUFFER      Self,
            _Out_ size_t* AvailableData
        );

    // Helpers
    _IRQL_requires_max_(DISPATCH_LEVEL)
        __forceinline size_t RingBufferCapacity(_In_ const PRING_BUFFER Self)
    {
        // Effective data capacity (we keep one byte unused to disambiguate empty/full)
        return (Self && Self->Size) ? (Self->Size - 1) : 0;
    }

    _IRQL_requires_max_(DISPATCH_LEVEL)
        __forceinline VOID RingBufferReset(_Inout_ PRING_BUFFER Self)
    {
        if (!Self) return;
        Self->Head = Self->Base;
        Self->Tail = Self->Base;
    }

#ifdef __cplusplus
}
#endif