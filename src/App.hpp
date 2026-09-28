#pragma once

#include "LaunchOptions.hpp"
#include "audio/IAudio.hpp"
#include "bar/Bar.hpp"
#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "modules/WindowIcons.hpp"
#include "network/INetwork.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/FrameClock.hpp"
#include "platform/Process.hpp"
#include "render/Graphics.hpp"
#include "tray/ShellNotifyIcon.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <cstdint>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Owns the config, the graphics devices and one bar per monitor, and rebuilds the bars when the config file changes.
class App {
  public:
    App(HINSTANCE instance, LaunchOptions options);
    ~App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until a bar is closed, then rethrows any error a window or the watcher reported.
    std::int32_t Run();

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void                    CreateBars(std::vector<BarPlacement> placements);
    void                    HandleConfigChanged();
    void                    ReloadConfig();
    // Creates the tray when the config shows one and there is none yet, or destroys it when the config shows none.
    void MatchTrayToConfig();
    void RebuildBarsIfMonitorsChanged();
    // Destroys the bars and schedules a new graphics device, which RecreateDevice builds the bars on.
    void                  HandleDeviceLost();
    void                  ScheduleDeviceRetry();
    void                  RecreateDevice();

    HINSTANCE             _instance;
    std::filesystem::path _configPath;
    std::uint16_t         _glazeWmPort;
    std::wstring          _trayClass;
    // Bars hold a reference to the config, so it is replaced only after they are destroyed.
    std::unique_ptr<Config> _config;
    // Replaced when the device is lost, after the bars that draw with it are destroyed.
    GraphicsDevice            _graphics;
    std::chrono::milliseconds _deviceRetry;
    bool                      _recovering    = false; // no bars exist until the new device is ready
    AppState                  _state         = {.error = nullptr};
    bool                      _redrawPending = false;
    std::exception_ptr        _configError; // the last reload's failure, fatal unless another save follows soon
    HWND                      _control;
    // Shared by every source of module data, which outlive none of it: declared before them all.
    ChangeSignal _changes;
    // Paces animations for every bar, which outlive none of it.
    FrameClock                   _frames;
    std::unique_ptr<FileWatcher> _watcher;
    // Starts the commands bound to module inputs, off the UI thread.
    Launcher _launcher;
    // Modules read the window manager, so it is created before the bars and destroyed after them.
    std::unique_ptr<IWindowManager> _windowManager;
    // Created before the bars and destroyed after them, like the window manager, since custom modules read them.
    std::unique_ptr<CommandPollers> _commandPollers;
    // Kept across reloads: icons belong to windows, not to a config. Trimmed to the listed windows on every change.
    std::unique_ptr<WindowIcons> _windowIcons;
    // Kept across reloads: the output device belongs to the system, not to a config.
    std::unique_ptr<IAudio> _audio;
    // Kept across reloads, like the audio device, and sampled whether or not a module shows it.
    std::unique_ptr<INetwork> _network;
    // Kept across reloads, like the network.
    std::unique_ptr<IBattery> _battery;
    // Null while the config shows no tray module, and kept across reloads that show one.
    std::unique_ptr<ShellNotifyIcon> _tray;
    // The monitors the bars were built for, compared on WM_DISPLAYCHANGE.
    std::vector<BarPlacement>         _placements;
    std::vector<std::unique_ptr<Bar>> _bars;
};
