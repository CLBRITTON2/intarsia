#pragma once

#include "bar/Bar.hpp"
#include "config/Config.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/Process.hpp"
#include "render/Graphics.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

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

    void                    CreateBars();
    void                    ReloadConfig();

    HINSTANCE               _instance;
    std::filesystem::path   _configPath;
    // Bars hold a reference to the config, so it is replaced only after they are destroyed.
    std::unique_ptr<Config>      _config;
    GraphicsDevice               _graphics;
    AppState                     _state;
    bool                         _redrawPending;
    HWND                         _control;
    std::unique_ptr<FileWatcher> _watcher;
    // Starts the commands bound to module inputs, off the UI thread.
    Launcher _launcher;
    // Modules read the window manager, so it is created before the bars and destroyed after them.
    std::unique_ptr<IWindowManager>   _windowManager;
    std::vector<std::unique_ptr<Bar>> _bars;
};
