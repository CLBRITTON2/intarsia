#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <iterator>
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
    bool          occupied; // holds at least one window
    std::intptr_t monitor;  // the HMONITOR the workspace is on

    bool          operator==(const Workspace&) const = default;
};

// A window on a monitor's displayed workspace.
struct ManagedWindow {
    std::string   id;     // what commands address the window by
    std::intptr_t handle; // the HWND
    bool          focused;
    std::intptr_t monitor; // the HMONITOR the window's workspace is on

    bool          operator==(const ManagedWindow&) const = default;
};

// The workspaces or windows on one monitor, in order.
template <typename Item>
    requires std::same_as<Item, Workspace> || std::same_as<Item, ManagedWindow>
std::vector<Item> OnMonitor(const std::vector<Item>& items, std::intptr_t monitor) {
    std::vector<Item> shown;
    std::ranges::copy_if(items, std::back_inserter(shown), [monitor](const Item& item) { return item.monitor == monitor; });
    return shown;
}

// The axis new windows tile along in the focused container.
enum class TilingDirection : std::uint8_t {
    Horizontal,
    Vertical
};

enum class Connection : std::uint8_t {
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
    // The windows on every monitor's displayed workspace, in the manager's tiling order.
    virtual std::vector<ManagedWindow> Windows() const = 0;
    // The display names of the active binding modes, empty when none is active.
    virtual std::vector<std::wstring> BindingModes() const = 0;
    // The focused container's tiling direction, or nullopt when nothing focused has one.
    virtual std::optional<TilingDirection> Direction() const = 0;
    virtual WindowManagerStatus            Status() const    = 0;

    // Asks the manager to focus a workspace from Workspaces(). Returns at once: the result arrives as a change.
    virtual void FocusWorkspace(const std::string& name) = 0;
    // Asks the manager to focus a window from Windows(), the same way.
    virtual void FocusWindow(const std::string& id) = 0;
    // Asks the manager to flip the focused container's tiling direction, the same way.
    virtual void ToggleTilingDirection() = 0;
    // Sends the manager a command from the config as written, such as "focus --next-workspace", the same way.
    virtual void RunCommand(const std::string& command) = 0;
};
