#include "App.hpp"

#include "platform/Errors.hpp"
#include "platform/Win32Cast.hpp"
#include "wm/WindowManagerFactory.hpp"

#include <shellscalingapi.h>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace {

    constexpr wchar_t kControlClass[] = L"IntarsiaControl";
    constexpr UINT    kConfigChanged  = WM_APP + 2;
    constexpr UINT    kWatcherFailed  = WM_APP + 3;
    constexpr UINT    kWindowsChanged = WM_APP + 4;
    // Editors save in several writes, so the reload waits until the file has been quiet this long.
    constexpr std::chrono::milliseconds kConfigSettle{100};

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
    instance_(instance), configPath_(std::move(configPath)), config_(std::make_unique<Config>(LoadConfig(configPath_))), graphics_(CreateGraphicsDevice()),
    state_{.error = nullptr}, control_(CreateControlWindow(instance, this, ControlProc)),
    watcher_(std::make_unique<FileWatcher>(configPath_, control_, kConfigChanged, kWatcherFailed, kConfigSettle)),
    windowManager_(CreateWindowManager(config_->windowManager, control_, kWindowsChanged)) {
    Bar::RegisterWindowClass(instance_);
    CreateBars();
}

App::~App() {
    bars_.clear();
    windowManager_.reset();
    watcher_.reset();
    DestroyWindow(control_);
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (state_.error) {
        std::rethrow_exception(state_.error);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::ControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<App*>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = IntToPointer<App>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    try {
        return self->HandleControlMessage(window, message, wParam, lParam);
    } catch (...) {
        self->state_.error = std::current_exception();
        PostQuitMessage(1);
        return 0;
    }
}

LRESULT App::HandleControlMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kConfigChanged: ReloadConfig(); return 0;
        case kWindowsChanged:
            for (const std::unique_ptr<Bar>& bar : bars_) {
                bar->Render();
            }
            return 0;
        case kWatcherFailed: {
            const std::exception_ptr error = watcher_->TakeError();
            if (!error) {
                throw std::logic_error("config watcher reported a failure without storing an error");
            }
            std::rethrow_exception(error);
        }
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void App::CreateBars() {
    for (const BarPlacement& placement : PlacementsForAllMonitors()) {
        bars_.push_back(
            std::make_unique<Bar>(instance_, graphics_, *config_, ModuleSources{.windowManager = *windowManager_, .monitor = placement.monitor}, launcher_, placement, state_));
        if (state_.error) {
            std::rethrow_exception(state_.error);
        }
    }
}

// An invalid config throws before any bar is touched, and the error ends the app like a bad config at startup.
void App::ReloadConfig() {
    auto config = std::make_unique<Config>(LoadConfig(configPath_));
    bars_.clear();
    // Reset before creating the replacement: the native backend allows one instance at a time.
    windowManager_.reset();
    config_        = std::move(config);
    windowManager_ = CreateWindowManager(config_->windowManager, control_, kWindowsChanged);
    CreateBars();
}
