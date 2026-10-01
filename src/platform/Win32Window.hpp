#pragma once

#include <windows.h>

#include <cstdint>
#include <string_view>

// Each throws Win32Error naming what, such as "tray", when its call fails.

// cursor is null for a window the pointer never rests on.
void RegisterWindowClass(HINSTANCE instance, const wchar_t* className, WNDPROC procedure, HCURSOR cursor, std::string_view what);

// A top-level WS_POPUP window of no size, which its owner places later. owner is the CreateWindowExW lpParam.
HWND CreatePopupWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, DWORD extendedStyle, void* owner, std::string_view what);

// Setting a timer that is already running restarts it.
void StartTimer(HWND window, UINT_PTR id, std::uint32_t milliseconds, std::string_view what);
