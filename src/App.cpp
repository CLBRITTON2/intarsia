#include "App.hpp"

#include "audio/CoreAudio.hpp"
#include "bar/Bar.hpp"
#include "battery/PowerStatus.hpp"
#include "modules/Network.hpp"
#include "network/IpHelper.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Monitors.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "tray/ShellNotifyIcon.hpp"
#include "wm/GlazeWm.hpp"
#include "wm/Native.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

    // RegisterClassExW takes a class name of up to 256 characters with its null.
    constexpr size_t        kMaxClassNameLength = 255;

    constexpr wchar_t       kControlClass[] = L"IntarsiaControl";
    constexpr std::uint32_t kConfigChanged  = WM_APP + 2;
    constexpr std::uint32_t kWatcherFailed  = WM_APP + 3;
    // Posted by the window manager backend and the command pollers when what modules show has changed.
    constexpr std::uint32_t kSourcesChanged = WM_APP + 4;
    constexpr std::uint32_t kDeviceLost     = WM_APP + 5;
    constexpr std::uint32_t kTrayFailed     = WM_APP + 6;
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
    // Windows is often still switching adapters when a device is lost, so the new device waits a moment, backing off
    // while each new device is lost before it draws a frame.
    constexpr UINT_PTR                  kDeviceTimer = 3;
    constexpr std::chrono::milliseconds kFirstDeviceRetry{500};
    constexpr std::chrono::milliseconds kLongestDeviceRetry{10000};
    // Long enough that a window with no icon at all costs a read only now and then.
    constexpr std::chrono::milliseconds kWindowIconRetry{10000};

    bool                                ReadsNetworkRates(const Config& config) {
        return AnyModuleInConfig(config, [](const ModuleInstance& instance) {
            const auto* network = std::get_if<NetworkConfig>(&instance.settings);
            return network != nullptr && ReadsRates(*network);
        });
    }

    // A source kept while the config shows a module of its kind, so its state survives reloads that keep it, and
    // dropped otherwise, since it samples the system for as long as it exists.
    template <typename Source, typename Create>
    std::unique_ptr<Source> MatchedToConfig(std::unique_ptr<Source> source, bool shown, const Create& create) {
        if (!shown) {
            return nullptr;
        }
        if (source != nullptr) {
            return source;
        }
        return create();
    }

    std::string YesOrNo(bool value) {
        if (value) {
            return "yes";
        }
        return "no";
    }

    // One line per target, so a user finds the id to write in bar.monitors.
    void LogMonitor(const MonitorDescription& description, bool shown) {
        const std::string primaryField = YesOrNo(description.primary);
        const std::string barField     = YesOrNo(shown);
        if (description.targets.empty()) {
            Log(LogLevel::Info, "app", "monitor", {{.key = "id", .value = "none"}, {.key = "primary", .value = primaryField}, {.key = "bar", .value = barField}});
            return;
        }
        for (const DisplayTarget& target : description.targets) {
            Log(LogLevel::Info, "app", "monitor",
                {{.key = "id", .value = target.id.value_or("none")},
                 {.key = "name", .value = target.name.value_or("none")},
                 {.key = "connection", .value = target.connection},
                 {.key = "primary", .value = primaryField},
                 {.key = "bar", .value = barField}});
        }
    }

    // Receives the worker threads' messages and survives the bars being rebuilt. A hidden top-level window rather than a
    // message-only one, because only top-level windows get the WM_DISPLAYCHANGE broadcast.
    HWND CreateControlWindow(HINSTANCE instance, void* owner, WNDPROC procedure) {
        RegisterWindowClass(instance, kControlClass, procedure, nullptr, "control");
        return CreatePopupWindow(instance, kControlClass, L"intarsia control", WS_EX_TOOLWINDOW, owner, "control");
    }

    std::unique_ptr<Native> FocusSourceFor(const Config& config, ChangeSignal& changes) {
        if (ShowsKind(config, ModuleKind::Window)) {
            return std::make_unique<Native>(changes);
        }
        return nullptr;
    }

    // The config's window manager backend. glazeWmPort is where the GlazeWM backend connects on 127.0.0.1, unused by the
    // others.
    std::unique_ptr<IWindowManager> CreateWindowManager(const Config& config, ChangeSignal& changes, std::uint16_t glazeWmPort) {
        switch (config.windowManager) {
            case WindowManagerKind::Windows: return std::make_unique<Native>(changes);
            case WindowManagerKind::GlazeWm: return std::make_unique<GlazeWm>(changes, glazeWmPort, FocusSourceFor(config, changes));
        }
        throw std::invalid_argument(std::format("unknown window manager kind {}", static_cast<std::int32_t>(config.windowManager)));
    }

} // namespace

LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, const std::filesystem::path& defaultLogPath,
                                 std::uint16_t defaultGlazeWmPort, const std::wstring& defaultTrayClass) {
    LaunchOptions options{
        .configPath  = defaultConfigPath,
        .logPath     = defaultLogPath,
        .glazeWmPort = defaultGlazeWmPort,
        .trayClass   = defaultTrayClass,
    };
    for (size_t index = 0; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (index + 1 >= arguments.size()) {
            throw LaunchOptionsError(std::format("{} needs a value", ToUtf8(name)));
        }
        const std::wstring& value = arguments[index + 1];
        if (name == L"--config") {
            options.configPath = value;
        } else if (name == L"--log") {
            options.logPath = value;
        } else if (name == L"--glazewm-port") {
            const std::string portText = ToUtf8(value);
            std::uint32_t     port     = 0;
            const auto [end, error]    = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            if (error != std::errc() || end != portText.data() + portText.size() || port == 0 || port > 65535) {
                throw LaunchOptionsError(std::format("--glazewm-port must be a port from 1 to 65535, got '{}'", portText));
            }
            options.glazeWmPort = static_cast<std::uint16_t>(port);
        } else if (name == L"--tray-class") {
            if (value.empty() || value.size() > kMaxClassNameLength) {
                throw LaunchOptionsError(std::format("--tray-class must be a window class name of 1 to {} characters, got {}", kMaxClassNameLength, value.size()));
            }
            options.trayClass = value;
        } else {
            throw LaunchOptionsError(std::format("unknown option '{}', intarsia takes --config <path>, --log <path>, --glazewm-port <port> and --tray-class <name>", ToUtf8(name)));
        }
    }
    return options;
}

App::App(HINSTANCE instance, LaunchOptions options) :
    _instance(instance), _configPath(std::move(options.configPath)), _glazeWmPort(options.glazeWmPort), _trayClass(std::move(options.trayClass)),
    _config(std::make_unique<Config>(LoadConfig(_configPath))), _graphics(CreateGraphicsDevice()), _deviceRetryWait(kFirstDeviceRetry),
    _control(CreateControlWindow(instance, this, OwnedWindowProc<App>)), _changes(_control.get(), kSourcesChanged),
    _watcher(_configPath.parent_path(), std::vector<std::wstring>{_configPath.filename().wstring(), StylePath(_configPath).filename().wstring()}, _control.get(), kConfigChanged,
             kWatcherFailed, kConfigSettle),
    _windowManager(CreateWindowManager(*_config, _changes, _glazeWmPort)), _commandPollers(std::make_unique<CommandPollers>(*_config, _changes)),
    _counterSamplers(std::make_unique<CounterSamplers>(*_config, _changes)) {
    MatchSourcesToConfig();
    Bar::RegisterWindowClass(_instance);
    Tooltip::RegisterWindowClass(_instance);
    Menu::RegisterWindowClass(_instance);
    CreateBars();
}

std::int32_t App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (_windowProcError.error) {
        std::rethrow_exception(_windowProcError.error);
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
        case kWatcherFailed: _watcher.RethrowFailure(); return 0;
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
        const std::vector<HMONITOR> monitors = ListMonitors();
        _placements                          = PlacementsFor(monitors);
        _monitorDescriptions                 = DescribeMonitors(monitors);
        for (size_t index = 0; index < monitors.size(); ++index) {
            const BarPlacement& placement = _placements[index];
            const bool          shown     = ShowsBarOn(_config->bar, _monitorDescriptions[index]);
            LogMonitor(_monitorDescriptions[index], shown);
            if (!shown) {
                continue;
            }
            _bars.push_back(std::make_unique<Bar>(_instance, _graphics, *_config,
                                                  ModuleSources{
                                                      .windowManager   = *_windowManager,
                                                      .commandPollers  = *_commandPollers,
                                                      .counterSamplers = *_counterSamplers,
                                                      .windowIcons     = _windowIcons.get(),
                                                      .audioOutput     = _audioOutput.get(),
                                                      .audioInput      = _audioInput.get(),
                                                      .network         = _network.get(),
                                                      .battery         = _battery.get(),
                                                      .tray            = _tray.get(),
                                                      .monitor         = placement.monitor,
                                                  },
                                                  _launcher, placement, AppBarRouteForTray(), _windowProcError, _control.get(), kDeviceLost));
            if (_windowProcError.error) {
                std::rethrow_exception(_windowProcError.error);
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
    _windowIcons = MatchedToConfig(std::move(_windowIcons), ShowsKind(*_config, ModuleKind::Windows), [this] { return std::make_unique<WindowIcons>(_changes, kWindowIconRetry); });
    _audioOutput =
        MatchedToConfig(std::move(_audioOutput), ShowsAudioFlow(*_config, AudioFlow::Output), [this] { return std::make_unique<CoreAudio>(_changes, AudioFlow::Output); });
    _audioInput = MatchedToConfig(std::move(_audioInput), ShowsAudioFlow(*_config, AudioFlow::Input), [this] { return std::make_unique<CoreAudio>(_changes, AudioFlow::Input); });
    const bool samplesRates = ReadsNetworkRates(*_config);
    if (_network != nullptr && _network->SamplesRates() != samplesRates) {
        _network.reset();
    }
    _network = MatchedToConfig(std::move(_network), ShowsKind(*_config, ModuleKind::Network), [this, samplesRates] { return std::make_unique<IpHelper>(_changes, samplesRates); });
    _battery = MatchedToConfig(std::move(_battery), ShowsKind(*_config, ModuleKind::Battery), [this] { return std::make_unique<PowerStatus>(_changes); });
    _tray    = MatchedToConfig(std::move(_tray), ShowsKind(*_config, ModuleKind::Tray), [this] {
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
    if (_tray != nullptr && _trayClass == kTaskbarClass) {
        return AppBarRoute::Taskbar;
    }
    return AppBarRoute::Shell;
}

// A monitor added, removed, moved, resized or rescaled gets fresh bars. The window manager and the command pollers
// stay, since they do not depend on monitors.
void App::RebuildBarsIfMonitorsChanged() {
    // A pending device recovery reads the monitors when it builds the bars.
    if (_recovering) {
        return;
    }
    if (!MonitorsDiffer(_placements, _monitorDescriptions)) {
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
    const std::chrono::milliseconds wait = RetryAfterLoss();
    _bars.clear();
    RetryDeviceLater("recovering_device", std::format("0x{:08X}", static_cast<std::uint32_t>(_graphics.d3d->GetDeviceRemovedReason())), wait);
}

// A device that drew a frame was working, so the wait starts short again.
std::chrono::milliseconds App::RetryAfterLoss() const {
    if (std::ranges::any_of(_bars, [](const std::unique_ptr<Bar>& bar) { return bar->HasPresented(); })) {
        return kFirstDeviceRetry;
    }
    return LongerDeviceRetry();
}

std::chrono::milliseconds App::LongerDeviceRetry() const {
    return std::min(_deviceRetryWait * 2, kLongestDeviceRetry);
}

void App::RetryDeviceLater(std::string_view event, std::string_view reason, std::chrono::milliseconds wait) {
    _deviceRetryWait = wait;
    _recovering      = true;
    Log(LogLevel::Warning, "app", event, {{.key = "reason", .value = std::string(reason)}, {.key = "retry_ms", .value = std::to_string(_deviceRetryWait.count())}});
    StartTimer(_control.get(), kDeviceTimer, static_cast<std::uint32_t>(_deviceRetryWait.count()), "device");
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
