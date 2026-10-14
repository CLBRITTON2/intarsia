#pragma once

#include "platform/Win32Cast.hpp"

#include <windows.h>
#include <windowsx.h>

#include <exception>
#include <memory>
#include <type_traits>

// The exception a window procedure caught, held for the message loop to rethrow, since exceptions cannot unwind through
// user32.
struct WindowProcError {
    std::exception_ptr error;
};

// The window procedure for a window created with its Owner as the CreateWindowExW lpParam. Owner provides
// HandleMessage(HWND, UINT, WPARAM, LPARAM) and a _windowProcError whose error member receives any exception HandleMessage
// throws: an exception cannot unwind through user32, so it is stored and the message loop ends instead.
template <typename Owner>
LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* owner = static_cast<Owner*>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
    }

    auto* owner = IntToPointer<Owner>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (owner == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    try {
        return owner->HandleMessage(window, message, wParam, lParam);
    } catch (...) {
        owner->_windowProcError.error = std::current_exception();
        PostQuitMessage(1);
        return 0;
    }
}

// Destroys a window made for OwnedWindowProc, detaching its owner first, whose other members may already be gone. As a
// member, it destroys the window even when the owner's constructor throws, which runs no destructor body.
struct OwnedWindowDestroyer {
    void operator()(HWND window) const {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
};

using UniqueOwnedWindow = std::unique_ptr<std::remove_pointer_t<HWND>, OwnedWindowDestroyer>;

// The client-area point a mouse message's lParam carries.
inline POINT ClientPoint(LPARAM lParam) {
    return POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
}
