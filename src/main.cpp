#include "bar/Bar.hpp"
#include "config/Config.hpp"
#include "platform/Errors.hpp"
#include "platform/Win32Cast.hpp"
#include "render/Graphics.hpp"

#include <windows.h>
#include <shellscalingapi.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

namespace {

    // Only collects handles: throwing out of an EnumDisplayMonitors callback would unwind through user32.
    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        IntToPointer<std::vector<HMONITOR>>(data)->push_back(monitor);
        return TRUE;
    }

    BarPlacement PlacementForMonitor(HMONITOR monitor) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            ThrowLastError("GetMonitorInfoW");
        }

        UINT dpiX = 0;
        UINT dpiY = 0;
        ThrowIfFailed(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY), "GetDpiForMonitor");
        return BarPlacement{.monitorRect = info.rcMonitor, .dpi = dpiX};
    }

    std::vector<BarPlacement> PlacementsForAllMonitors() {
        std::vector<HMONITOR> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors))) {
            ThrowLastError("EnumDisplayMonitors");
        }

        std::vector<BarPlacement> placements;
        placements.reserve(monitors.size());
        for (HMONITOR monitor : monitors) {
            placements.push_back(PlacementForMonitor(monitor));
        }
        return placements;
    }

    int Run(HINSTANCE instance) {
        const Config         config   = LoadConfig(DefaultConfigPath());
        const GraphicsDevice graphics = CreateGraphicsDevice();
        Bar::RegisterWindowClass(instance);

        AppState                          state{.liveBars = 0, .error = nullptr};
        std::vector<std::unique_ptr<Bar>> bars;
        for (const BarPlacement& placement : PlacementsForAllMonitors()) {
            bars.push_back(std::make_unique<Bar>(instance, graphics, config, placement, state));
            if (state.error) {
                std::rethrow_exception(state.error);
            }
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (state.error) {
            std::rethrow_exception(state.error);
        }
        return static_cast<int>(message.wParam);
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        return Run(instance);
    } catch (const std::exception& error) {
        // stderr reaches a launching shell that redirected it, the message box covers a plain double-click.
        std::fprintf(stderr, "intarsia: %s\n", error.what());
        MessageBoxA(nullptr, error.what(), "intarsia", MB_ICONERROR | MB_OK);
        return 1;
    }
}
