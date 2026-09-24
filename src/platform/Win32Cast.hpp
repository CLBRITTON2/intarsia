#pragma once

#include <windows.h>

// Win32 carries pointers in integer message params and window longs, so this cast is unavoidable.
template <typename T>
T* IntToPointer(LONG_PTR value) {
    return reinterpret_cast<T*>(value); // NOLINT(performance-no-int-to-ptr)
}
