#pragma once

#include <windows.h>

#include <memory>
#include <string_view>

struct HandleCloser {
    void operator()(HANDLE handle) const {
        CloseHandle(handle);
    }
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// Takes ownership of a handle a Win32 call returned, throwing with that call's last error when it is null or
// INVALID_HANDLE_VALUE (CreateFileW fails with the latter, most other creators with null).
UniqueHandle OwnHandle(HANDLE handle, std::string_view call);
