#pragma once

#include <windows.h>

enum class Edge {
    Top,
    Bottom,
    Left,
    Right
};

// Strip of the given thickness along one edge of area.
RECT DockedRect(const RECT& area, Edge edge, int thickness);

void RegisterAppBar(HWND window, UINT callbackMessage);
void UnregisterAppBar(HWND window);

// Asks the shell for space on the monitor and reserves it. The shell may shrink the request around other appbars.
RECT ReserveAppBarRect(HWND window, const RECT& monitorRect, Edge edge, int thickness);

void NotifyAppBarActivated(HWND window);
void NotifyAppBarMoved(HWND window);
