#pragma once

#include "bar/Bar.hpp"
#include "config/Config.hpp"
#include "platform/FileWatcher.hpp"
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
    static LRESULT CALLBACK ControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleControlMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void                    CreateBars();
    void                    ReloadConfig();

    HINSTANCE               instance_;
    std::filesystem::path   configPath_;
    // Bars hold a reference to the config, so it is replaced only after they are destroyed.
    std::unique_ptr<Config>      config_;
    GraphicsDevice               graphics_;
    AppState                     state_;
    HWND                         control_;
    std::unique_ptr<FileWatcher> watcher_;
    // Modules read the window manager, so it is created before the bars and destroyed after them.
    std::unique_ptr<IWindowManager>   windowManager_;
    std::vector<std::unique_ptr<Bar>> bars_;
};
