#pragma once

#include "bar/Bar.hpp"
#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/Process.hpp"
#include "render/Graphics.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

// Owns the config, the graphics devices and one bar per monitor, and rebuilds the bars when the config file changes.
class App {
  public:
    App(HINSTANCE instance, std::filesystem::path configPath);
    ~App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Runs the message loop until a bar is closed, then rethrows any error a window or the watcher reported.
    int Run();

  private:
    template <typename Owner>
    friend LRESULT CALLBACK OwnedWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void                    CreateBars(std::vector<BarPlacement> placements);
    void                    ReloadConfig();
    void                    RebuildBarsIfMonitorsChanged();
    // Destroys the bars and schedules a new graphics device, which RecreateDevice builds the bars on.
    void                  HandleDeviceLost();
    void                  ScheduleDeviceRetry();
    void                  RecreateDevice();

    HINSTANCE             _instance;
    std::filesystem::path _configPath;
    // Bars hold a reference to the config, so it is replaced only after they are destroyed.
    std::unique_ptr<Config> _config;
    // Replaced when the device is lost, after the bars that draw with it are destroyed.
    GraphicsDevice               _graphics;
    std::chrono::milliseconds    _deviceRetry;
    bool                         _recovering; // no bars exist until the new device is ready
    AppState                     _state;
    bool                         _redrawPending;
    HWND                         _control;
    std::unique_ptr<FileWatcher> _watcher;
    // Starts the commands bound to module inputs, off the UI thread.
    Launcher _launcher;
    // Modules read the window manager, so it is created before the bars and destroyed after them.
    std::unique_ptr<IWindowManager> _windowManager;
    // Created before the bars and destroyed after them, like the window manager, since custom modules read them.
    std::unique_ptr<CommandPollers> _commandPollers;
    // The monitors the bars were built for, compared on WM_DISPLAYCHANGE.
    std::vector<BarPlacement>         _placements;
    std::vector<std::unique_ptr<Bar>> _bars;
};
