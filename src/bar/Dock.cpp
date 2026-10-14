#include "bar/Dock.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "render/Axis.hpp"
#include "render/Graphics.hpp"

#include <shellscalingapi.h>

#include <format>
#include <stdexcept>

namespace {

    BarPlacement PlacementForMonitor(HMONITOR monitor) {
        std::uint32_t dpiX     = 0;
        std::uint32_t dpiY     = 0;
        const HRESULT dpiFound = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (FAILED(dpiFound)) {
            throw MonitorGoneError(std::format("GetDpiForMonitor failed with 0x{:08X} for a listed handle", static_cast<std::uint32_t>(dpiFound)));
        }
        return BarPlacement{
            .monitor     = monitor,
            .monitorRect = MonitorInfo(monitor).rcMonitor,
            .dpi         = dpiX,
        };
    }

    float EdgeLengthPixels(const RECT& monitorRect, Edge edge) {
        if (AxisOf(edge) == Axis::Horizontal) {
            return static_cast<float>(monitorRect.right - monitorRect.left);
        }
        return static_cast<float>(monitorRect.bottom - monitorRect.top);
    }

    RECT ReserveRect(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness, AppBarRoute route) {
        switch (route) {
            case AppBarRoute::Shell: return ReserveAppBarRect(window, monitorRect, edge, thickness);
            case AppBarRoute::Taskbar: {
                HWND taskbar = ExplorerTaskbar(kTaskbarClass);
                // No Explorer, no one to reserve space with: SHAppBarMessage would find no taskbar either.
                if (taskbar == nullptr) {
                    Log(LogLevel::Warning, "bar", "taskbar_missing", {});
                    return DockedRect(monitorRect, edge, thickness);
                }
                return ReserveAppBarRectThroughTaskbar(window, monitorRect, edge, thickness, taskbar);
            }
        }
        throw std::invalid_argument(std::format("unknown appbar route {}", static_cast<std::int32_t>(route)));
    }

} // namespace

bool BarPlacement::operator==(const BarPlacement& other) const {
    return monitor == other.monitor && EqualRect(&monitorRect, &other.monitorRect) && dpi == other.dpi;
}

std::vector<BarPlacement> PlacementsFor(const std::vector<HMONITOR>& monitors) {
    std::vector<BarPlacement> placements;
    placements.reserve(monitors.size());
    for (HMONITOR monitor : monitors) {
        placements.push_back(PlacementForMonitor(monitor));
    }
    return placements;
}

// A monitor swapped for another at the same place and scale changes only its description. A monitor that goes while it
// is read has changed too.
bool MonitorsDiffer(const std::vector<BarPlacement>& placements, const std::vector<MonitorDescription>& descriptions) {
    try {
        const std::vector<HMONITOR> monitors = ListMonitors();
        return PlacementsFor(monitors) != placements || DescribeMonitors(monitors) != descriptions;
    } catch (const MonitorGoneError&) { return true; }
}

float EdgeLengthDips(const BarPlacement& placement, Edge edge) {
    return PixelsToDips(EdgeLengthPixels(placement.monitorRect, edge), placement.dpi);
}

HWND LayerZOrder(Layer layer) {
    switch (layer) {
        case Layer::Top: return HWND_TOPMOST;
        case Layer::Bottom: return HWND_BOTTOM;
    }
    throw std::invalid_argument(std::format("unknown layer {}", static_cast<std::int32_t>(layer)));
}

// A shared bar stays a registered appbar, so the shell still tells it of fullscreen apps and of other appbars moving.
RECT BarRect(HWND window, const RECT& monitorRect, const BarConfig& bar, std::int32_t thickness, AppBarRoute route) {
    switch (bar.space) {
        case Space::Reserve: return ReserveRect(window, monitorRect, bar.edge, thickness, route);
        case Space::Share: return DockedRect(monitorRect, bar.edge, thickness);
    }
    throw std::invalid_argument(std::format("unknown space {}", static_cast<std::int32_t>(bar.space)));
}
