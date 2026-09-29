#pragma once

#include <windows.h>

// After windows.h, which declares what it uses, so in a block of its own.
#include <shellapi.h>

#include <cstdint>
#include <string>

enum class Edge : std::uint8_t {
    Top,
    Bottom,
    Left,
    Right
};

// COPYDATASTRUCT::dwData of an SHAppBarMessage call to the taskbar window.
inline constexpr ULONG_PTR kAppBarCopyData = 0;

// APPBARDATA as shell32 sends it to the taskbar window, with the window 32 bits wide, and as the taskbar writes a
// result back.
struct AppBarData32 {
    std::uint32_t size; // sizeof(AppBarData32)
    std::uint32_t window;
    std::uint32_t callbackMessage;
    std::uint32_t edge;
    RECT          rect;
    std::int64_t  lParam;
};

// The WM_COPYDATA payload of an SHAppBarMessage call, as shell32 sends it on 64-bit Windows (no public layout).
struct AppBarCommand {
    AppBarData32  data;
    std::uint32_t message; // ABM_*
    std::uint32_t reserved;
    // Where the taskbar writes the AppBarData32 of ABM_QUERYPOS and ABM_SETPOS, zero for the other messages: a
    // SHAllocShared handle that resultProcess holds.
    std::uint64_t result;
    std::uint32_t resultProcess;
    std::uint32_t reserved2;
};

// The request shell32 sends the taskbar for one SHAppBarMessage call.
AppBarCommand AppBarRequest(std::uint32_t message, const APPBARDATA& data, std::uint64_t result, std::uint32_t resultProcess);

// Which way a bar's own appbar requests reach the taskbar.
enum class AppBarRoute : std::uint8_t {
    // SHAppBarMessage, which sends to the first taskbar window in z-order.
    Shell,
    // Straight to Explorer's taskbar window: while this process's own tray window is the first taskbar window, shell32
    // answers the process's own calls in-process with a result Explorer cannot open, and drops them.
    Taskbar
};

// Strip of the given thickness along one edge of area.
RECT DockedRect(const RECT& area, Edge edge, std::int32_t thickness);

// left,top,right,bottom, as log values write a rect.
std::string RectText(const RECT& rect);

// The message a taskbar broadcasts once it starts. Explorer sends it after a restart, having forgotten every appbar.
std::uint32_t TaskbarCreatedMessage();

void          RegisterAppBar(HWND window, std::uint32_t callbackMessage);
void          UnregisterAppBar(HWND window);

// Asks the shell for space on the monitor and reserves it. The shell may shrink the request around other appbars. Logs
// each answer and the monitor's work area after it, since the shell never reports a request it ignored.
RECT ReserveAppBarRect(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness);

// ReserveAppBarRect asked of taskbar, Explorer's taskbar window, in the form shell32 would send. A taskbar that does
// not answer within a moment leaves the request as it is, logged, as SHAppBarMessage would.
RECT ReserveAppBarRectThroughTaskbar(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness, HWND taskbar);

void NotifyAppBarActivated(HWND window);
void NotifyAppBarMoved(HWND window);
