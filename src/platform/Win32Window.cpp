#include "platform/Win32Window.hpp"

#include "platform/Errors.hpp"

#include <format>

void RegisterWindowClass(HINSTANCE instance, const wchar_t* className, WNDPROC procedure, HCURSOR cursor, std::string_view what) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = cursor;
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0) {
        ThrowLastError(std::format("RegisterClassExW({})", what));
    }
}

HWND CreatePopupWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, DWORD extendedStyle, void* owner, std::string_view what) {
    HWND window = CreateWindowExW(extendedStyle, className, title, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, owner);
    if (window == nullptr) {
        ThrowLastError(std::format("CreateWindowExW({})", what));
    }
    return window;
}

void StartTimer(HWND window, UINT_PTR id, std::uint32_t milliseconds, std::string_view what) {
    if (SetTimer(window, id, milliseconds, nullptr) == 0) {
        ThrowLastError(std::format("SetTimer({})", what));
    }
}
