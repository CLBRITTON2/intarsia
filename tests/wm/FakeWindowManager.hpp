#pragma once

#include "wm/IWindowManager.hpp"

#include <optional>
#include <string>
#include <vector>

// A backend whose state a test sets directly, recording the commands modules send it.
class FakeWindowManager final : public IWindowManager {
  public:
    std::optional<FocusedWindow>   focused;
    std::vector<Workspace>         workspaces;
    std::vector<ManagedWindow>     windows;
    std::vector<std::wstring>      bindingModes;
    std::optional<TilingDirection> direction;
    std::string                    focusedWorkspace;
    std::string                    focusedWindow;
    int                            directionToggles = 0;
    std::vector<std::string>       commands;

    std::optional<FocusedWindow>   Focused() const override {
        return focused;
    }
    std::vector<Workspace> Workspaces() const override {
        return workspaces;
    }
    std::vector<ManagedWindow> Windows() const override {
        return windows;
    }
    std::vector<std::wstring> BindingModes() const override {
        return bindingModes;
    }
    std::optional<TilingDirection> Direction() const override {
        return direction;
    }
    WindowManagerStatus Status() const override {
        return WindowManagerStatus{.name = L"fake", .connection = Connection::Connected, .detail = ""};
    }
    void FocusWorkspace(const std::string& name) override {
        focusedWorkspace = name;
    }
    void FocusWindow(const std::string& id) override {
        focusedWindow = id;
    }
    void ToggleTilingDirection() override {
        ++directionToggles;
    }
    void RunCommand(const std::string& command) override {
        commands.push_back(command);
    }
};
