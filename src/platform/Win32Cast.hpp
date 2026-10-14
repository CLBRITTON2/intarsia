#pragma once

#include <windows.h>

#include <type_traits>

// Win32 carries pointers in integer message params and window longs, so this cast is unavoidable.
template <typename T>
T* IntToPointer(LONG_PTR value) {
    return reinterpret_cast<T*>(value); // NOLINT(performance-no-int-to-ptr)
}

inline HWND ToWindow(LONG_PTR value) {
    return IntToPointer<std::remove_pointer_t<HWND>>(value);
}

inline HICON ToIcon(LONG_PTR value) {
    return IntToPointer<std::remove_pointer_t<HICON>>(value);
}
