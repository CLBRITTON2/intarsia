#include "App.hpp"

#include "audio/CoreAudio.hpp"
#include "battery/PowerStatus.hpp"
#include "modules/Network.hpp"
#include "network/IpHelper.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Win32Cast.hpp"
#include "platform/Win32Window.hpp"
#include "platform/WindowProc.hpp"
#include "tray/ShellNotifyIcon.hpp"
#include "wm/WindowManagerFactory.hpp"

#include <shellscalingapi.h>

#include <cstdint>
#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace {

    constexpr wchar_t       kControlClass[] = L"IntarsiaControl";
    constexpr std::uint32_t kConfigChanged  = WM_APP + 2;
    constexpr std::uint32_t kWatcherFailed  = WM_APP + 3;
    // Posted by the window manager backend and the command pollers when what modules show has changed.
    constexpr std::uint32_t kSourcesChanged = WM_APP + 4;
    // Editors save in several writes, so the reload waits until the file has been quiet this long.
    constexpr std::chrono::milliseconds kConfigSettle{100};
    // How long a config that failed to load must stay unchanged before the failure ends the app.
    constexpr UINT_PTR                  kConfigFailedTimer = 4;
    constexpr std::chrono::milliseconds kConfigConfirm{1000};
    // Window manager changes arrive in bursts (a terminal can retitle itself many times a second), so redraws they
    // cause are held to one per frame.
    constexpr UINT_PTR      kRedrawTimer   = 1;
    constexpr std::uint32_t kRedrawDelayMs = 16;
    // Docking or undocking sends several WM_DISPLAYCHANGE while monitors come up, so bars are rebuilt once it is quiet.
    constexpr UINT_PTR      kDisplaysTimer    = 2;
    constexpr std::uint32_t kDisplaysSettleMs = 500;
    constexpr std::uint32_t kDeviceLost       = WM_APP + 5;
    constexpr std::uint32_t kTrayFailed       = WM_APP + 6;
    // Windows is often still switching adapters when a device is lost, so the new device waits a moment, backing off
    // while each new device is lost before it draws a frame.
    constexpr UINT_PTR                  kDeviceTimer = 3;
    constexpr std::chrono::milliseconds kFirstDeviceRetry{500};
    constexpr std::chrono::milliseconds kLongestDeviceRetry{10000};
    // Long enough that a window with no icon at all costs a read only now and then.
    constexpr std::chrono::milliseconds kWindowIconRetry{10000};

    bool                                ShowsAudioFlow(const Config& config, AudioFlow flow) {
        return AnyModuleInConfig(config, [flow](const ModuleInstance& instance) {
            const auto* audio = std::get_if<AudioConfig>(&instance.settings);
            return audio != nullptr && audio->flow == flow;
        });
    }

    bool ReadsNetworkRates(const Config& config) {
        return AnyModuleInConfig(config, [](const ModuleInstance& instance) {
            const auto* network = std::get_if<NetworkConfig>(&instance.settings);
            return network != nullptr && ReadsRates(*network);
        });
    }

    // A source kept while the config shows a module of its kind, so its state survives reloads that keep it, and
    // dropped otherwise, since it samples the system for as long as it exists.
    template <typename Source>
    std::unique_ptr<Source> MatchedToConfig(std::unique_ptr<Source> source, bool shown, const std::function<std::unique_ptr<Source>()>& create) {
        if (!shown) {
            return nullptr;
        }
        if (source != nullptr) {
            return source;
        }
        return create();
    }

    // Only collects handles: throwing out of an EnumDisplayMonitors callback would unwind through user32.
    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        IntToPointer<std::vector<HMONITOR>>(data)->push_back(monitor);
        return TRUE;
    }

    BarPlacement PlacementForMonitor(HMONITOR monitor) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
            throw MonitorGoneError("GetMonitorInfoW found no monitor for a listed handle");
        }

        std::uint32_t dpiX     = 0;
        std::uint32_t dpiY     = 0;
        const HRESULT dpiFound = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (FAILED(dpiFound)) {
            throw MonitorGoneError(std::format("GetDpiForMonitor failed with 0x{:08X} for a listed handle", static_cast<std::uint32_t>(dpiFound)));
        }
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

    // A monitor that goes while it is read has changed too.
    bool MonitorsDiffer(const std::vector<BarPlacement>& placements) {
        try {
            return PlacementsForAllMonitors() != placements;
        } catch (const MonitorGoneError&) { return true; }
    }

    // Receives the worker threads' messages and survives the bars being rebuilt. A hidden top-level window rather than a
    // message-only one, because only top-level windows get the WM_DISPLAYCHANGE broadcast.
    HWND CreateControlWindow(HINSTANCE instance, void* owner, WNDPROC procedure) {
        RegisterWindowClass(instance, kControlClass, procedure, nullptr, "control");
        return CreatePopupWindow(instance, kControlClass, L"intarsia control", WS_EX_TOOLWINDOW, owner, "control");
    }

} // namespace

App::App(HINSTANCE instance, LaunchOptions options) :
    _instance(instance), _configPath(std::move(options.configPath)), _glazeWmPort(options.glazeWmPort), _trayClass(std::move(options.trayClass)),
    _config(std::make_unique<Config>(LoadConfig(_configPath))), _graphics(CreateGraphicsDevice()), _deviceRetry(kFirstDeviceRetry),
    _control(CreateControlWindow(instance, this, OwnedWindowProc<App>)), _changes(_control.get(), kSourcesChanged),
    _watcher(std::make_unique<FileWatcher>(_configPath.parent_path(), std::vector<std::wstring>{_configPath.filename().wstring(), StylePath(_configPath).filename().wstring()},
                                           _control.get(), kConfigChanged, kWatcherFailed, kConfigSettle)),
    _windowManager(CreateWindowManager(*_config, _changes, _glazeWmPort)), _commandPollers(std::make_unique<CommandPollers>(*_config, _changes)),
    _counterSamplers(std::make_unique<CounterSamplers>(*_config, _changes)) {
    MatchSourcesToConfig();
    Bar::RegisterWindowClass(_instance);
    Tooltip::RegisterWindowClass(_instance);
    Menu::RegisterWindowClass(_instance);
    CreateBars();
}

App::~App() {
    _bars.clear();
    _tray.reset();
    _battery.reset();
    _network.reset();
    _audioInput.reset();
    _audioOutput.reset();
    _windowIcons.reset();
    _counterSamplers.reset();
    _commandPollers.reset();
    _windowManager.reset();
    _watcher.reset();
}

std::int32_t App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (_state.error) {
        std::rethrow_exception(_state.error);
    }
    return static_cast<std::int32_t>(message.wParam);
}

LRESULT App::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case kConfigChanged: HandleConfigChanged(); return 0;
        case kDeviceLost: HandleDeviceLost(); return 0;
        case kSourcesChanged:
            if (!_redrawPending) {
                StartTimer(_control.get(), kRedrawTimer, kRedrawDelayMs, "redraw");
                _redrawPending = true;
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // Setting the timer again restarts it, so the rebuild waits for the last change in a burst.
            StartTimer(_control.get(), kDisplaysTimer, kDisplaysSettleMs, "displays");
            return 0;
        case WM_TIMER:
            if (wParam == kRedrawTimer) {
                KillTimer(_control.get(), kRedrawTimer);
                _redrawPending = false;
                _changes.Acknowledge();
                if (_windowIcons != nullptr) {
                    _windowIcons->ForgetClosed();
                }
                for (const std::unique_ptr<Bar>& bar : _bars) {
                    bar->Refresh();
                }
            } else if (wParam == kDisplaysTimer) {
                KillTimer(_control.get(), kDisplaysTimer);
                RebuildBarsIfMonitorsChanged();
            } else if (wParam == kDeviceTimer) {
                KillTimer(_control.get(), kDeviceTimer);
                RecreateDevice();
            } else if (wParam == kConfigFailedTimer) {
                KillTimer(_control.get(), kConfigFailedTimer);
                std::rethrow_exception(_configError);
            }
            return 0;
        case kWatcherFailed: _watcher->RethrowFailure(); return 0;
        case kTrayFailed:
            // Any process on the desktop can post this message, even while no tray exists.
            if (_tray == nullptr) {
                Log(LogLevel::Warning, "app", "spurious_tray_failure", {});
                return 0;
            }
            _tray->RethrowFailure();
            return 0;
        // taskkill sends WM_CLOSE to every top-level window, this hidden one included, and DefWindowProcW would destroy
        // it. The bars' own WM_CLOSE can be lost when a reload replaces them first, so this one quits too.
        case WM_CLOSE: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void App::CreateBars() {
    try {
        _placements = PlacementsForAllMonitors();
        for (const BarPlacement& placement : _placements) {
            _bars.push_back(std::make_unique<Bar>(_instance, _graphics, *_config,
                                                  ModuleSources{.windowManager   = *_windowManager,
                                                                .commandPollers  = *_commandPollers,
                                                                .counterSamplers = *_counterSamplers,
                                                                .windowIcons     = _windowIcons.get(),
                                                                .audioOutput     = _audioOutput.get(),
                                                                .audioInput      = _audioInput.get(),
                                                                .network         = _network.get(),
                                                                .battery         = _battery.get(),
                                                                .tray            = _tray.get(),
                                                                .monitor         = placement.monitor},
                                                  _launcher, placement, AppBarRouteForTray(), _state, _control.get(), kDeviceLost));
            if (_state.error) {
                std::rethrow_exception(_state.error);
            }
        }
    } catch (const DeviceLostError& error) {
        // Lost, or a monitor gone, while a bar was being built rather than while it drew: the same recovery, and it had
        // drawn nothing.
        _bars.clear();
        RetryDeviceLater("recovering_device", error.what(), LongerDeviceRetry());
    }
}

// A config that fails to load leaves the running bars alone. The failure ends the app like a bad config at startup,
// but only once the file has stayed unchanged for kConfigConfirm: a reload can read a file an editor is still writing
// (truncated, not yet filled), and the next save supersedes that read.
void App::HandleConfigChanged() {
    KillTimer(_control.get(), kConfigFailedTimer);
    _configError = nullptr;
    try {
        ReloadConfig();
    } catch (const ConfigError& error) {
        Log(LogLevel::Warning, "app", "config_invalid", {{.key = "reason", .value = error.what()}, {.key = "fatal_in_ms", .value = std::to_string(kConfigConfirm.count())}});
        _configError = std::current_exception();
        StartTimer(_control.get(), kConfigFailedTimer, static_cast<std::uint32_t>(kConfigConfirm.count()), "config failed");
    }
}

// Throws ConfigError, with no bar touched, when the config does not load.
void App::ReloadConfig() {
    auto config = std::make_unique<Config>(LoadConfig(_configPath));
    _bars.clear();
    _counterSamplers.reset();
    _commandPollers.reset();
    // Reset before creating the replacement: the native backend allows one instance at a time.
    _windowManager.reset();
    _config          = std::move(config);
    _windowManager   = CreateWindowManager(*_config, _changes, _glazeWmPort);
    _commandPollers  = std::make_unique<CommandPollers>(*_config, _changes);
    _counterSamplers = std::make_unique<CounterSamplers>(*_config, _changes);
    MatchSourcesToConfig();
    // A pending device recovery builds the bars from this config once it has a device.
    if (!_recovering) {
        CreateBars();
    }
}

void App::MatchSourcesToConfig() {
    _windowIcons = MatchedToConfig<WindowIcons>(std::move(_windowIcons), ShowsKind(*_config, ModuleKind::Windows),
                                                [this] { return std::make_unique<WindowIcons>(_changes, kWindowIconRetry); });
    _audioOutput =
        MatchedToConfig<IAudio>(std::move(_audioOutput), ShowsAudioFlow(*_config, AudioFlow::Output), [this] { return std::make_unique<CoreAudio>(_changes, AudioFlow::Output); });
    _audioInput =
        MatchedToConfig<IAudio>(std::move(_audioInput), ShowsAudioFlow(*_config, AudioFlow::Input), [this] { return std::make_unique<CoreAudio>(_changes, AudioFlow::Input); });
    const bool samplesRates = ReadsNetworkRates(*_config);
    if (_network != nullptr && _network->SamplesRates() != samplesRates) {
        _network.reset();
    }
    _network = MatchedToConfig<IpHelper>(std::move(_network), ShowsKind(*_config, ModuleKind::Network),
                                         [this, samplesRates] { return std::make_unique<IpHelper>(_changes, samplesRates); });
    _battery = MatchedToConfig<IBattery>(std::move(_battery), ShowsKind(*_config, ModuleKind::Battery), [this] { return std::make_unique<PowerStatus>(_changes); });
    _tray    = MatchedToConfig<ShellNotifyIcon>(std::move(_tray), ShowsKind(*_config, ModuleKind::Tray), [this] {
        auto tray = std::make_unique<ShellNotifyIcon>(_changes, _trayClass, _control.get(), kTrayFailed);
        // Apps add their icons again for the taskbar's class only, so a tray of another class has no one to tell.
        if (_trayClass == kTaskbarClass) {
            AnnounceTray();
        }
        return tray;
    });
}

// While the tray is the first taskbar window, the bars' own SHAppBarMessage calls would land in this process and never
// reach Explorer.
AppBarRoute App::AppBarRouteForTray() const {
    return _tray != nullptr && _trayClass == kTaskbarClass ? AppBarRoute::Taskbar : AppBarRoute::Shell;
}

// A monitor added, removed, moved, resized or rescaled gets fresh bars. The window manager and the command pollers
// stay, since they do not depend on monitors.
void App::RebuildBarsIfMonitorsChanged() {
    // A pending device recovery reads the monitors when it builds the bars.
    if (_recovering) {
        return;
    }
    if (!MonitorsDiffer(_placements)) {
        return;
    }
    Log(LogLevel::Info, "app", "monitors_changed", {{.key = "from", .value = std::to_string(_placements.size())}});
    _bars.clear();
    CreateBars();
}

// Every bar reports the same loss, so reports that arrive after the bars were destroyed find no lost bar and do nothing.
// Retries forever, as the GlazeWM backend does: a GPU that comes back must not need the bar restarted.
void App::HandleDeviceLost() {
    if (std::ranges::none_of(_bars, [](const std::unique_ptr<Bar>& bar) { return bar->DeviceLost(); })) {
        return;
    }
    // A device that drew a frame was working, so the wait starts short again.
    const bool drew = std::ranges::any_of(_bars, [](const std::unique_ptr<Bar>& bar) { return bar->HasPresented(); });
    _bars.clear();
    RetryDeviceLater("recovering_device", std::format("0x{:08X}", static_cast<std::uint32_t>(_graphics.d3d->GetDeviceRemovedReason())),
                     drew ? kFirstDeviceRetry : LongerDeviceRetry());
}

std::chrono::milliseconds App::LongerDeviceRetry() const {
    return std::min(_deviceRetry * 2, kLongestDeviceRetry);
}

void App::RetryDeviceLater(std::string_view event, std::string_view reason, std::chrono::milliseconds wait) {
    _deviceRetry = wait;
    _recovering  = true;
    Log(LogLevel::Warning, "app", event, {{.key = "reason", .value = std::string(reason)}, {.key = "retry_ms", .value = std::to_string(_deviceRetry.count())}});
    StartTimer(_control.get(), kDeviceTimer, static_cast<std::uint32_t>(_deviceRetry.count()), "device");
}

// Creating the device fails while no adapter is ready, and is lost when the adapter goes again mid-creation (back to back
// losses on re-enabling an adapter): both wait longer and try again.
void App::RecreateDevice() {
    try {
        _graphics = CreateGraphicsDevice();
    } catch (const Win32Error& error) {
        RetryDeviceLater("device_unavailable", error.what(), LongerDeviceRetry());
        return;
    } catch (const DeviceLostError& error) {
        RetryDeviceLater("device_unavailable", error.what(), LongerDeviceRetry());
        return;
    }
    _recovering = false;
    CreateBars();
}
