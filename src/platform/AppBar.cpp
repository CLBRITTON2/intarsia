#include "platform/AppBar.hpp"

#include <shellapi.h>

#include <format>
#include <stdexcept>

namespace {

    UINT ShellEdge(Edge edge) {
        switch (edge) {
            case Edge::Top: return ABE_TOP;
            case Edge::Bottom: return ABE_BOTTOM;
            case Edge::Left: return ABE_LEFT;
            case Edge::Right: return ABE_RIGHT;
        }
        throw std::invalid_argument(std::format("unknown edge {}", static_cast<int>(edge)));
    }

    APPBARDATA AppBarData(HWND window) {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd   = window;
        return data;
    }

} // namespace

RECT DockedRect(const RECT& area, Edge edge, int thickness) {
    RECT rect = area;
    switch (edge) {
        case Edge::Top: rect.bottom = rect.top + thickness; return rect;
        case Edge::Bottom: rect.top = rect.bottom - thickness; return rect;
        case Edge::Left: rect.right = rect.left + thickness; return rect;
        case Edge::Right: rect.left = rect.right - thickness; return rect;
    }
    throw std::invalid_argument(std::format("unknown edge {}", static_cast<int>(edge)));
}

void RegisterAppBar(HWND window, UINT callbackMessage) {
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

RECT ReserveAppBarRect(HWND window, const RECT& monitorRect, Edge edge, int thickness) {
    APPBARDATA data = AppBarData(window);
    data.uEdge      = ShellEdge(edge);
    data.rc         = monitorRect;
    SHAppBarMessage(ABM_QUERYPOS, &data);
    // QUERYPOS only moves the side facing the edge, so the thickness is reapplied from that side.
    data.rc = DockedRect(data.rc, edge, thickness);
    SHAppBarMessage(ABM_SETPOS, &data);
    return data.rc;
}

void NotifyAppBarActivated(HWND window) {
    APPBARDATA data = AppBarData(window);
    SHAppBarMessage(ABM_ACTIVATE, &data);
}

void NotifyAppBarMoved(HWND window) {
    APPBARDATA data = AppBarData(window);
    SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
}
