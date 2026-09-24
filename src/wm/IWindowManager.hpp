#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct FocusedWindow {
    std::wstring title;
};

struct Workspace {
    std::wstring  label; // the display name when the manager has one, else the name
    std::string   name;  // what commands address the workspace by
    bool          focused;
    std::intptr_t monitor; // the HMONITOR the workspace is on

    bool          operator==(const Workspace&) const = default;
};

enum class Connection {
    Connected,
    Disconnected
};

struct WindowManagerStatus {
    std::wstring name; // shown in the disconnected indicator, such as "GlazeWM"
    Connection   connection;
    std::string  detail; // why the last connection attempt failed, empty while connected

    bool         operator==(const WindowManagerStatus&) const = default;
};

// The bar's view of whatever manages windows: a tiling window manager over IPC, or Windows itself. Lives on the UI
// thread, and posts its change message to the target window it was created with whenever what it reports changes.
class IWindowManager {
  public:
    virtual ~IWindowManager() = default;

    virtual std::optional<FocusedWindow> Focused() const    = 0;
    virtual std::vector<Workspace>       Workspaces() const = 0;
    virtual WindowManagerStatus          Status() const     = 0;

    // Asks the manager to focus a workspace from Workspaces(). Returns at once: the result arrives as a change.
    virtual void FocusWorkspace(const std::string& name) = 0;
};
