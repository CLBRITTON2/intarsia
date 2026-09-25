#pragma once

#include "platform/Win32Cast.hpp"

#include <windows.h>

#include <exception>

// The window procedure for a window created with its Owner as the CreateWindowExW lpParam. Owner provides
// HandleMessage(HWND, UINT, WPARAM, LPARAM) and a _state whose error member receives any exception HandleMessage
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
        owner->_state.error = std::current_exception();
        PostQuitMessage(1);
        return 0;
    }
}
