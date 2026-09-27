#pragma once

#include <windows.h>

#include <cstdint>

enum class Edge : std::uint8_t {
    Top,
    Bottom,
    Left,
    Right
};

// Strip of the given thickness along one edge of area.
RECT DockedRect(const RECT& area, Edge edge, std::int32_t thickness);

void RegisterAppBar(HWND window, std::uint32_t callbackMessage);
void UnregisterAppBar(HWND window);

// Asks the shell for space on the monitor and reserves it. The shell may shrink the request around other appbars.
RECT ReserveAppBarRect(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness);

void NotifyAppBarActivated(HWND window);
void NotifyAppBarMoved(HWND window);
