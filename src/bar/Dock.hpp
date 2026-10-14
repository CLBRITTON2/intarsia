#pragma once

#include "config/Config.hpp"
#include "platform/AppBar.hpp"
#include "platform/Monitors.hpp"

#include <windows.h>

#include <cstdint>
#include <vector>

// The monitor a bar docks to, where it is on the desktop and its scale.
struct BarPlacement {
    HMONITOR      monitor;
    RECT          monitorRect;
    std::uint32_t dpi;

    bool          operator==(const BarPlacement& other) const;
};

// Each monitor's placement, in the order given. Throws MonitorGoneError for a monitor that went while it was read.
std::vector<BarPlacement> PlacementsFor(const std::vector<HMONITOR>& monitors);

// Whether the monitors now differ from placements and descriptions, as a monitor added, removed, moved, resized,
// rescaled or swapped for another makes them.
bool MonitorsDiffer(const std::vector<BarPlacement>& placements, const std::vector<MonitorDescription>& descriptions);

// The whole monitor's length along the bar's edge, which picks the config's layout.
float EdgeLengthDips(const BarPlacement& placement, Edge edge);

// Where SetWindowPos puts a bar of layer in the z-order.
HWND LayerZOrder(Layer layer);

// The bar window's rect on monitorRect, thickness pixels across, reserved with the shell through route when bar
// reserves space.
RECT BarRect(HWND window, const RECT& monitorRect, const BarConfig& bar, std::int32_t thickness, AppBarRoute route);
