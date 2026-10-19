#pragma once

#include <windows.h>

#include <memory>
#include <string_view>
#include <type_traits>

// Drops Free's result, which a deleter has no way to report.
template <auto Free>
struct ReleaseWith {
    template <typename Pointer>
    void operator()(Pointer pointer) const {
        static_cast<void>(Free(pointer));
    }
};

template <typename Handle, auto Free>
using UniqueReleased = std::unique_ptr<std::remove_pointer_t<Handle>, ReleaseWith<Free>>;

using UniqueHandle = UniqueReleased<HANDLE, &CloseHandle>;
using UniqueLocal = UniqueReleased<HLOCAL, &LocalFree>;

// Takes ownership of a handle a Win32 call returned, throwing with that call's last error when it is null or
// INVALID_HANDLE_VALUE (CreateFileW fails with the latter, most other creators with null).
UniqueHandle OwnHandle(HANDLE handle, std::string_view call);

// Unsignaled events, named in the CreateEventW error they throw.
UniqueHandle AutoResetEvent(std::string_view name);
UniqueHandle ManualResetEvent(std::string_view name);
