#include "platform/AppBar.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"

#include <shellapi.h>
#include <shlwapi.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

    static_assert(sizeof(AppBarData32) == 40, "AppBarData32 must match shell32's layout");
    static_assert(sizeof(AppBarCommand) == 64, "AppBarCommand must match shell32's layout");

    // The bar waits on the answer, so a hung Explorer must not hold it for long.
    constexpr std::uint32_t kTaskbarTimeoutMs = 2000;

    std::uint32_t           ShellEdge(Edge edge) {
        switch (edge) {
            case Edge::Top: return ABE_TOP;
            case Edge::Bottom: return ABE_BOTTOM;
            case Edge::Left: return ABE_LEFT;
            case Edge::Right: return ABE_RIGHT;
        }
        throw std::invalid_argument(std::format("unknown edge {}", static_cast<std::int32_t>(edge)));
    }

    APPBARDATA AppBarData(HWND window) {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd   = window;
        return data;
    }

    // Frees a SHAllocShared handle that process holds.
    struct FreeShared {
        DWORD process;

        void  operator()(HANDLE handle) const {
            SHFreeShared(handle, process);
        }
    };

    // Sends the request as shell32 does and copies the taskbar's answer back into data.
    void SendToTaskbar(HWND taskbar, DWORD taskbarProcess, std::uint32_t message, APPBARDATA& data) {
        // The taskbar reads the request from the block it answers in, since shell32 sets only the block's size.
        AppBarData32 answer = AppBarRequest(message, data, 0, 0).data;
        HANDLE       block  = SHAllocShared(&answer, sizeof(answer), taskbarProcess);
        if (block == nullptr) {
            ThrowLastError("SHAllocShared(appbar result)");
        }
        AppBarCommand  request = AppBarRequest(message, data, reinterpret_cast<std::uint64_t>(block), taskbarProcess);
        COPYDATASTRUCT copy{.dwData = kAppBarCopyData, .cbData = sizeof(request), .lpData = &request};
        DWORD_PTR      result = 0;
        if (SendMessageTimeoutW(taskbar, WM_COPYDATA, reinterpret_cast<WPARAM>(data.hWnd), reinterpret_cast<LPARAM>(&copy), SMTO_ABORTIFHUNG, kTaskbarTimeoutMs, &result) == 0) {
            Log(LogLevel::Warning, "bar", "taskbar_unanswered", {{.key = "message", .value = std::to_string(message)}, {.key = "error", .value = std::to_string(GetLastError())}});
            // A timed-out send can still reach Explorer, which then writes its answer through the block: freeing it
            // could let that write land in whatever reuses the handle, so the block stays Explorer's.
            return;
        }
        const std::unique_ptr<void, FreeShared> answered(block, FreeShared{.process = taskbarProcess});
        void*                                   view = SHLockShared(block, taskbarProcess);
        if (view == nullptr) {
            ThrowLastError("SHLockShared(appbar result)");
        }
        std::memcpy(&answer, view, sizeof(answer));
        SHUnlockShared(view);
        data.rc = answer.rect;
    }

    RECT WorkArea(const RECT& monitorRect) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromRect(&monitorRect, MONITOR_DEFAULTTONEAREST), &info)) {
            ThrowLastError("GetMonitorInfoW(appbar work area)");
        }
        return info.rcWork;
    }

    // QUERYPOS only moves the side facing the edge, so the thickness is reapplied from that side before SETPOS.
    RECT Reserve(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness, const std::function<void(std::uint32_t, APPBARDATA&)>& send) {
        APPBARDATA data = AppBarData(window);
        data.uEdge      = ShellEdge(edge);
        data.rc         = monitorRect;
        send(ABM_QUERYPOS, data);
        const RECT queried = data.rc;
        data.rc            = DockedRect(data.rc, edge, thickness);
        send(ABM_SETPOS, data);
        Log(LogLevel::Info, "bar", "appbar_reserved",
            {{.key = "monitor", .value = RectText(monitorRect)},
             {.key = "queried", .value = RectText(queried)},
             {.key = "set", .value = RectText(data.rc)},
             {.key = "work_area", .value = RectText(WorkArea(monitorRect))}});
        return data.rc;
    }

} // namespace

AppBarCommand AppBarRequest(std::uint32_t message, const APPBARDATA& data, std::uint64_t result, std::uint32_t resultProcess) {
    return AppBarCommand{.data          = AppBarData32{.size            = sizeof(AppBarData32),
                                                       .window          = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data.hWnd)),
                                                       .callbackMessage = data.uCallbackMessage,
                                                       .edge            = data.uEdge,
                                                       .rect            = data.rc,
                                                       .lParam          = static_cast<std::int64_t>(data.lParam)},
                         .message       = message,
                         .reserved      = 0,
                         .result        = result,
                         .resultProcess = resultProcess,
                         .reserved2     = 0};
}

RECT DockedRect(const RECT& area, Edge edge, std::int32_t thickness) {
    RECT rect = area;
    switch (edge) {
        case Edge::Top: rect.bottom = rect.top + thickness; return rect;
        case Edge::Bottom: rect.top = rect.bottom - thickness; return rect;
        case Edge::Left: rect.right = rect.left + thickness; return rect;
        case Edge::Right: rect.left = rect.right - thickness; return rect;
    }
    throw std::invalid_argument(std::format("unknown edge {}", static_cast<std::int32_t>(edge)));
}

std::string RectText(const RECT& rect) {
    return std::format("{},{},{},{}", rect.left, rect.top, rect.right, rect.bottom);
}

std::uint32_t TaskbarCreatedMessage() {
    const UINT created = RegisterWindowMessageW(L"TaskbarCreated");
    if (created == 0) {
        ThrowLastError("RegisterWindowMessageW(TaskbarCreated)");
    }
    return created;
}

void RegisterAppBar(HWND window, std::uint32_t callbackMessage) {
    APPBARDATA data       = AppBarData(window);
    data.uCallbackMessage = callbackMessage;
    if (!SHAppBarMessage(ABM_NEW, &data)) {
        throw std::runtime_error("SHAppBarMessage(ABM_NEW) refused the window, it is already an appbar");
    }
}

void UnregisterAppBar(HWND window) {
    APPBARDATA data = AppBarData(window);
    SHAppBarMessage(ABM_REMOVE, &data);
}

RECT ReserveAppBarRect(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness) {
    return Reserve(window, monitorRect, edge, thickness, [](std::uint32_t message, APPBARDATA& data) { SHAppBarMessage(message, &data); });
}

RECT ReserveAppBarRectThroughTaskbar(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness, HWND taskbar) {
    DWORD taskbarProcess = 0;
    if (GetWindowThreadProcessId(taskbar, &taskbarProcess) == 0) {
        ThrowLastError("GetWindowThreadProcessId(taskbar)");
    }
    return Reserve(window, monitorRect, edge, thickness,
                   [taskbar, taskbarProcess](std::uint32_t message, APPBARDATA& data) { SendToTaskbar(taskbar, taskbarProcess, message, data); });
}

void NotifyAppBarActivated(HWND window) {
    APPBARDATA data = AppBarData(window);
    SHAppBarMessage(ABM_ACTIVATE, &data);
}

void NotifyAppBarMoved(HWND window) {
    APPBARDATA data = AppBarData(window);
    SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
}
