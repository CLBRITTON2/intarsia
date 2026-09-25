#include "App.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Win32Cast.hpp"
#include "platform/WindowProc.hpp"
#include "wm/WindowManagerFactory.hpp"

#include <shellscalingapi.h>

#include <chrono>
#include <exception>
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

    // A message-only window: it receives the watcher's messages and survives the bars being rebuilt.
    HWND CreateControlWindow(HINSTANCE instance, void* owner, WNDPROC procedure) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize        = sizeof(windowClass);
        windowClass.lpfnWndProc   = procedure;
        windowClass.hInstance     = instance;
        windowClass.lpszClassName = kControlClass;
        if (RegisterClassExW(&windowClass) == 0) {
            ThrowLastError("RegisterClassExW(control)");
        }
        HWND window = CreateWindowExW(0, kControlClass, L"intarsia control", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, owner);
        if (window == nullptr) {
            ThrowLastError("CreateWindowExW(control)");
        }
        return window;
    }

} // namespace

App::App(HINSTANCE instance, std::filesystem::path configPath) :
    _instance(instance), _configPath(std::move(configPath)), _config(std::make_unique<Config>(LoadConfig(_configPath))), _graphics(CreateGraphicsDevice()),
    _state{.error = nullptr}, _redrawPending(false), _control(CreateControlWindow(instance, this, OwnedWindowProc<App>)),
    _watcher(std::make_unique<FileWatcher>(_configPath, _control, kConfigChanged, kWatcherFailed, kConfigSettle)),
    _windowManager(CreateWindowManager(_config->windowManager, _control, kSourcesChanged)), _commandPollers(std::make_unique<CommandPollers>(*_config, _control, kSourcesChanged)) {
    Bar::RegisterWindowClass(_instance);
    CreateBars();
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
        case kSourcesChanged:
            if (!_redrawPending) {
                if (SetTimer(_control, kRedrawTimer, kRedrawDelayMs, nullptr) == 0) {
                    ThrowLastError("SetTimer(redraw)");
                }
                _redrawPending = true;
            }
            return 0;
        case WM_TIMER:
            if (wParam == kRedrawTimer) {
                KillTimer(_control, kRedrawTimer);
                _redrawPending = false;
                for (const std::unique_ptr<Bar>& bar : _bars) {
                    bar->Render();
                }
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

void App::CreateBars() {
    for (const BarPlacement& placement : PlacementsForAllMonitors()) {
        _bars.push_back(std::make_unique<Bar>(_instance, _graphics, *_config,
                                              ModuleSources{.windowManager = *_windowManager, .commandPollers = *_commandPollers, .monitor = placement.monitor}, _launcher,
                                              placement, _state));
        if (_state.error) {
            std::rethrow_exception(_state.error);
        }
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
    CreateBars();
}
