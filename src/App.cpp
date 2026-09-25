#include "App.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Win32Cast.hpp"
#include "platform/WindowProc.hpp"
#include "wm/WindowManagerFactory.hpp"

#include <shellscalingapi.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <string>
#include <utility>

namespace {

    constexpr wchar_t kControlClass[] = L"IntarsiaControl";
    constexpr UINT    kConfigChanged  = WM_APP + 2;
    constexpr UINT    kWatcherFailed  = WM_APP + 3;
    // Posted by the window manager backend and the command pollers when what modules show has changed.
    constexpr UINT kSourcesChanged = WM_APP + 4;
    // Editors save in several writes, so the reload waits until the file has been quiet this long.
    constexpr std::chrono::milliseconds kConfigSettle{100};
    // Window manager changes arrive in bursts (a terminal can retitle itself many times a second), so redraws they
    // cause are held to one per frame.
    constexpr UINT_PTR kRedrawTimer   = 1;
    constexpr UINT     kRedrawDelayMs = 16;
    // Docking or undocking sends several WM_DISPLAYCHANGE while monitors come up, so bars are rebuilt once it is quiet.
    constexpr UINT_PTR kDisplaysTimer    = 2;
    constexpr UINT     kDisplaysSettleMs = 500;
    constexpr UINT     kDeviceLost       = WM_APP + 5;
    // Windows is often still switching adapters when a device is lost, so the new device waits a moment, backing off
    // while each new device is lost before it draws a frame.
    constexpr UINT_PTR                  kDeviceTimer = 3;
    constexpr std::chrono::milliseconds kFirstDeviceRetry{500};
    constexpr std::chrono::milliseconds kLongestDeviceRetry{10000};

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
        return BarPlacement{.monitor = monitor, .monitorRect = info.rcMonitor, .dpi = dpiX};
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

    // Receives the worker threads' messages and survives the bars being rebuilt. A hidden top-level window rather than a
    // message-only one, because only top-level windows get the WM_DISPLAYCHANGE broadcast.
    HWND CreateControlWindow(HINSTANCE instance, void* owner, WNDPROC procedure) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize        = sizeof(windowClass);
        windowClass.lpfnWndProc   = procedure;
        windowClass.hInstance     = instance;
        windowClass.lpszClassName = kControlClass;
        if (RegisterClassExW(&windowClass) == 0) {
            ThrowLastError("RegisterClassExW(control)");
        }
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kControlClass, L"intarsia control", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, owner);
        if (window == nullptr) {
            ThrowLastError("CreateWindowExW(control)");
        }
        return window;
    }

} // namespace

App::App(HINSTANCE instance, std::filesystem::path configPath) :
    _instance(instance), _configPath(std::move(configPath)), _config(std::make_unique<Config>(LoadConfig(_configPath))), _graphics(CreateGraphicsDevice()),
    _deviceRetry(kFirstDeviceRetry), _recovering(false), _state{.error = nullptr}, _redrawPending(false), _control(CreateControlWindow(instance, this, OwnedWindowProc<App>)),
    _watcher(std::make_unique<FileWatcher>(_configPath, _control, kConfigChanged, kWatcherFailed, kConfigSettle)),
    _windowManager(CreateWindowManager(_config->windowManager, _control, kSourcesChanged)), _commandPollers(std::make_unique<CommandPollers>(*_config, _control, kSourcesChanged)) {
    Bar::RegisterWindowClass(_instance);
    Tooltip::RegisterWindowClass(_instance);
    CreateBars(PlacementsForAllMonitors());
}

App::~App() {
    _bars.clear();
    _commandPollers.reset();
    _windowManager.reset();
    _watcher.reset();
    DestroyWindow(_control);
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (_state.error) {
        std::rethrow_exception(_state.error);
    }
    return static_cast<int>(message.wParam);
}

LRESULT App::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kConfigChanged: ReloadConfig(); return 0;
        case kDeviceLost: HandleDeviceLost(); return 0;
        case kSourcesChanged:
            if (!_redrawPending) {
                if (SetTimer(_control, kRedrawTimer, kRedrawDelayMs, nullptr) == 0) {
                    ThrowLastError("SetTimer(redraw)");
                }
                _redrawPending = true;
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // Setting the timer again restarts it, so the rebuild waits for the last change in a burst.
            if (SetTimer(_control, kDisplaysTimer, kDisplaysSettleMs, nullptr) == 0) {
                ThrowLastError("SetTimer(displays)");
            }
            return 0;
        case WM_TIMER:
            if (wParam == kRedrawTimer) {
                KillTimer(_control, kRedrawTimer);
                _redrawPending = false;
                for (const std::unique_ptr<Bar>& bar : _bars) {
                    bar->Render();
                }
            } else if (wParam == kDisplaysTimer) {
                KillTimer(_control, kDisplaysTimer);
                RebuildBarsIfMonitorsChanged();
            } else if (wParam == kDeviceTimer) {
                KillTimer(_control, kDeviceTimer);
                RecreateDevice();
            }
            return 0;
        case kWatcherFailed: {
            const std::exception_ptr error = _watcher->TakeError();
            // Any process on the desktop can post this message, so one with nothing stored behind it is not fatal.
            if (!error) {
                Log(LogLevel::Warning, "app", "spurious_watcher_failure", {});
                return 0;
            }
            std::rethrow_exception(error);
        }
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void App::CreateBars(std::vector<BarPlacement> placements) {
    _placements = std::move(placements);
    try {
        for (const BarPlacement& placement : _placements) {
            _bars.push_back(std::make_unique<Bar>(_instance, _graphics, *_config,
                                                  ModuleSources{.windowManager = *_windowManager, .commandPollers = *_commandPollers, .monitor = placement.monitor}, _launcher,
                                                  placement, _state, _control, kDeviceLost));
            if (_state.error) {
                std::rethrow_exception(_state.error);
            }
        }
    } catch (const DeviceLostError& error) {
        // Lost while a bar was being built rather than while it drew: the same recovery, and it had drawn nothing.
        _bars.clear();
        _deviceRetry = std::min(_deviceRetry * 2, kLongestDeviceRetry);
        Log(LogLevel::Warning, "app", "recovering_device", {{.key = "reason", .value = error.what()}, {.key = "retry_ms", .value = std::to_string(_deviceRetry.count())}});
        ScheduleDeviceRetry();
    }
}

// An invalid config throws before any bar is touched, and the error ends the app like a bad config at startup.
void App::ReloadConfig() {
    auto config = std::make_unique<Config>(LoadConfig(_configPath));
    _bars.clear();
    _commandPollers.reset();
    // Reset before creating the replacement: the native backend allows one instance at a time.
    _windowManager.reset();
    _config         = std::move(config);
    _windowManager  = CreateWindowManager(_config->windowManager, _control, kSourcesChanged);
    _commandPollers = std::make_unique<CommandPollers>(*_config, _control, kSourcesChanged);
    // A pending device recovery builds the bars from this config once it has a device.
    if (!_recovering) {
        CreateBars(PlacementsForAllMonitors());
    }
}

// A monitor added, removed, moved, resized or rescaled gets fresh bars. The window manager and the command pollers
// stay, since they do not depend on monitors.
void App::RebuildBarsIfMonitorsChanged() {
    // A pending device recovery reads the monitors when it builds the bars.
    if (_recovering) {
        return;
    }
    std::vector<BarPlacement> placements = PlacementsForAllMonitors();
    if (placements == _placements) {
        return;
    }
    Log(LogLevel::Info, "app", "monitors_changed", {{.key = "from", .value = std::to_string(_placements.size())}, {.key = "to", .value = std::to_string(placements.size())}});
    _bars.clear();
    CreateBars(std::move(placements));
}

// Every bar reports the same loss, so reports that arrive after the bars were destroyed find no lost bar and do nothing.
// Retries forever, as the GlazeWM backend does: a GPU that comes back must not need the bar restarted.
void App::HandleDeviceLost() {
    if (std::ranges::none_of(_bars, [](const std::unique_ptr<Bar>& bar) { return bar->DeviceLost(); })) {
        return;
    }
    // A device that drew a frame was working, so the wait starts short again.
    const bool drew = std::ranges::any_of(_bars, [](const std::unique_ptr<Bar>& bar) { return bar->HasPresented(); });
    _deviceRetry    = drew ? kFirstDeviceRetry : std::min(_deviceRetry * 2, kLongestDeviceRetry);
    Log(LogLevel::Warning, "app", "recovering_device",
        {{.key = "reason", .value = std::format("0x{:08X}", static_cast<unsigned long>(_graphics.d3d->GetDeviceRemovedReason()))},
         {.key = "retry_ms", .value = std::to_string(_deviceRetry.count())}});
    _bars.clear();
    ScheduleDeviceRetry();
}

void App::ScheduleDeviceRetry() {
    _recovering = true;
    if (SetTimer(_control, kDeviceTimer, static_cast<UINT>(_deviceRetry.count()), nullptr) == 0) {
        ThrowLastError("SetTimer(device)");
    }
}

// Creating the device fails while no adapter is ready, which waits longer and tries again.
void App::RecreateDevice() {
    try {
        _graphics = CreateGraphicsDevice();
    } catch (const Win32Error& error) {
        _deviceRetry = std::min(_deviceRetry * 2, kLongestDeviceRetry);
        Log(LogLevel::Warning, "app", "device_unavailable", {{.key = "reason", .value = error.what()}, {.key = "retry_ms", .value = std::to_string(_deviceRetry.count())}});
        ScheduleDeviceRetry();
        return;
    }
    _recovering = false;
    CreateBars(PlacementsForAllMonitors());
}
