#ifndef _PCH_H_
#define _PCH_H_
#pragma once

#include <ntddk.h>
#include <wdf.h>

#define K_MOVE(x) static_cast<decltype(x)&&>(x)

namespace vcom {
    constexpr ULONG poolTag = 'sDDs';

    template <typename T>
    class PoolPtr {
    public:
        PoolPtr() noexcept
            : _ptr(nullptr), _tag(0) {
        }

        PoolPtr(size_t count, ULONG tag = poolTag, POOL_FLAGS flags = NonPagedPoolNx) noexcept
            : _ptr(nullptr), _tag(tag)
        {
            _ptr = static_cast<T*>(::ExAllocatePool2(flags, count * sizeof(T), _tag));
        }

        ~PoolPtr() noexcept { reset(); }

        T* get() const noexcept { return _ptr; }

        T* release() noexcept {
            T* p = _ptr;
            _ptr = nullptr;
            return p;
        }

        void reset() noexcept {
            if (_ptr) {
                ::ExFreePoolWithTag(_ptr, _tag);
                _ptr = nullptr;
            }
        }

        // Non-copyable
        PoolPtr(const PoolPtr&) = delete;
        PoolPtr& operator=(const PoolPtr&) = delete;

        // Movable
        PoolPtr(PoolPtr&& other) noexcept
            : _ptr(other._ptr), _tag(other._tag) {
            other._ptr = nullptr;
            other._tag = 0;
        }

        PoolPtr& operator=(PoolPtr&& other) noexcept {
            if (this != &other) {
                reset();
                _ptr = other._ptr;
                _tag = other._tag;
                other._ptr = nullptr;
                other._tag = 0;
            }
            return *this;
        }

    private:
        T* _ptr;
        ULONG _tag;
    };

    class UnicodeStr {
    public:
        UnicodeStr() noexcept {
			::RtlInitEmptyUnicodeString(&_str, nullptr, 0);
        }

        NTSTATUS allocate(USHORT maxChars) noexcept {
            _buf = PoolPtr<WCHAR>(maxChars);
            if (!_buf.get()) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            ::RtlInitEmptyUnicodeString(&_str, _buf.get(), maxChars * sizeof(WCHAR));
            return STATUS_SUCCESS;
        }

        PUNICODE_STRING get() noexcept { return &_str; }
        PCUNICODE_STRING get() const noexcept { return &_str; }

        // Non-copyable
        UnicodeStr(const UnicodeStr&) = delete;
        UnicodeStr& operator=(const UnicodeStr&) = delete;

        // Movable
        UnicodeStr(UnicodeStr&& other) noexcept
            : _buf(K_MOVE(other._buf))  // move buffer first
        {
            _str = other._str;
            other._str.Buffer = nullptr;
            other._str.Length = 0;
            other._str.MaximumLength = 0;
        }

        UnicodeStr& operator=(UnicodeStr&& other) noexcept {
            if (this != &other) {
                _buf = K_MOVE(other._buf);
                _str = other._str;
                other._str.Buffer = nullptr;
                other._str.Length = 0;
                other._str.MaximumLength = 0;
            }
            return *this;
        }

    private:
        UNICODE_STRING _str;
        PoolPtr<WCHAR> _buf;
    };

}

#endif // _PCH_H_
