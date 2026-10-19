#pragma once

#include "wm/IWindowManager.hpp"

#include <optional>
#include <string>
#include <vector>

// A backend whose state a test sets directly, recording the workspace modules focus.
class FakeWindowManager final : public IWindowManager {
  public:
    std::optional<FocusedWindow> focused;
    std::vector<Workspace> workspaces;
    std::vector<ManagedWindow> windows;
    std::vector<std::wstring> bindingModes;
    std::optional<TilingDirection> direction;
    std::string focusedWorkspace;

    std::optional<FocusedWindow> Focused() const override {
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
        return WindowManagerStatus{
            .name = L"fake",
            .connection = Connection::Connected,
            .detail = "",
        };
    }
    void FocusWorkspace(const std::string& name) override {
        focusedWorkspace = name;
    }
    void FocusWindow(const std::string&) override {}
    void ToggleTilingDirection() override {}
    void RunCommand(const std::string&) override {}
};
