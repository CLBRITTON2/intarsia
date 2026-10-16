#pragma once

#include "audio/IAudio.hpp"
#include "bar/Bar.hpp"
#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/Counter.hpp"
#include "modules/Custom.hpp"
#include "modules/WindowList.hpp"
#include "network/IpHelper.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/Launcher.hpp"
#include "platform/Monitors.hpp"
#include "platform/Threads.hpp"
#include "platform/Win32Window.hpp"
#include "render/Graphics.hpp"
#include "tray/ShellNotifyIcon.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The command line: intarsia.exe [--config <path>] [--log <path>] [--glazewm-port <port>] [--tray-class <name>].
struct LaunchOptions {
    std::filesystem::path configPath;
    // The soak tests log beside their config, since a running bar keeps the default log open.
    std::filesystem::path logPath;
    std::uint16_t         glazeWmPort; // the soak tests point the bar at its own server while GlazeWM holds 6123
    // The soak tests give the tray a class no app sends to, so it never takes the desktop's icons.
    std::wstring trayClass;
};

class LaunchOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws LaunchOptionsError naming the bad argument.
LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, const std::filesystem::path& defaultLogPath,
                                 std::uint16_t defaultGlazeWmPort, const std::wstring& defaultTrayClass);

// Owns the config, the graphics devices and one bar per matched monitor, and rebuilds the bars when the config file changes.
class App {
  public:
    App(HINSTANCE instance, LaunchOptions options);
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until a bar is closed, then rethrows any error a window or the watcher reported.
    std::int32_t Run();

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    // One bar for each monitor present that bar.monitors matches.
    void CreateBars();
    void HandleConfigChanged();
    void ReloadConfig();
    // Creates each data source the config shows a module for and there is none of yet, and destroys those it shows
    // none for.
    void        MatchSourcesToConfig();
    AppBarRoute AppBarRouteForTray() const;
    void        RebuildBarsIfMonitorsChanged();
    // Destroys the bars and schedules a new graphics device, which RecreateDevice builds the bars on.
    void HandleDeviceLost();
    // The wait before a new device replaces the bars' lost one, read before the bars go.
    std::chrono::milliseconds RetryAfterLoss() const;
    // Twice the last wait, up to kLongestDeviceRetry.
    std::chrono::milliseconds LongerDeviceRetry() const;
    // Logs event with reason and schedules the next try after wait.
    void                  RetryDeviceLater(std::string_view event, std::string_view reason, std::chrono::milliseconds wait);
    void                  RecreateDevice();

    HINSTANCE             _instance;
    std::filesystem::path _configPath;
    std::uint16_t         _glazeWmPort;
    std::wstring          _trayClass;
    // Bars hold a reference to the config, so it is replaced only after they are destroyed.
    std::unique_ptr<Config> _config;
    // Outlives every compositor the graphics devices hold.
    CompositionThread _compositionThread;
    // Replaced when the device is lost, after the bars that draw with it are destroyed.
    GraphicsDevice            _graphics;
    std::chrono::milliseconds _deviceRetryWait;
    bool                      _recovering      = false; // no bars exist until the new device is ready
    WindowProcError           _windowProcError = {.error = nullptr};
    bool                      _redrawPending   = false;
    std::exception_ptr        _configError; // the last reload's failure, fatal unless another save follows soon
    UniqueOwnedWindow         _control;
    // Shared by every source of module data, which outlive none of it: declared before them all.
    ChangeSignal _changes;
    FileWatcher  _watcher;
    // Starts the commands bound to module inputs, off the UI thread.
    Launcher _launcher;
    // Modules read the window manager, so it is created before the bars and destroyed after them.
    std::unique_ptr<IWindowManager> _windowManager;
    // Created before the bars and destroyed after them, like the window manager, since custom and counter modules read
    // them.
    std::unique_ptr<CommandPollers>  _commandPollers;
    std::unique_ptr<CounterSamplers> _counterSamplers;
    // Each null while the config shows no module of its kind, and kept across reloads that show one, since its state
    // (icons, the default devices) belongs to the system, not to a config. Window icons are trimmed to the listed
    // windows on every change. Each audio flow is a kind of its own.
    std::unique_ptr<WindowIcons>     _windowIcons;
    std::unique_ptr<IAudio>          _audioOutput;
    std::unique_ptr<IAudio>          _audioInput;
    std::unique_ptr<IpHelper>        _network;
    std::unique_ptr<IBattery>        _battery;
    std::unique_ptr<ShellNotifyIcon> _tray;
    // Every monitor present when the bars were built, shown or not, compared on WM_DISPLAYCHANGE.
    std::vector<BarPlacement>         _placements;
    std::vector<MonitorDescription>   _monitorDescriptions;
    std::vector<std::unique_ptr<Bar>> _bars;
};
