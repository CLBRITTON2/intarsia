#pragma once

#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct LabelRun {
    std::uint32_t start;
    std::uint32_t length;
    bool          focused;
    std::string   name; // the workspace name commands use

    bool          operator==(const LabelRun& other) const = default;
};

struct WorkspaceLine {
    std::wstring          text;
    std::vector<LabelRun> runs;

    bool                  operator==(const WorkspaceLine& other) const = default;
};

// The workspaces on this bar's monitor, the focused one in the accent color, and a click focuses the one under it.
// While the window manager is unreachable it shows "<name> disconnected" in the alert color instead.
class Workspaces final : public IModule {
  public:
    Workspaces(IWindowManager& windowManager, HMONITOR monitor);

    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    // None: the module has no config table, and a left click focuses the workspace under it.
    const ModuleActions& Actions() const override;
    // While disconnected, why: the window manager's status detail. Nothing while connected.
    std::optional<std::wstring> TooltipText() override;

  private:
    IWindowManager& _windowManager;
    std::intptr_t   _monitor;
    // What _text holds, once it has a layout.
    WorkspaceLine _line;
    bool          _disconnected = false;
    IconText      _text;
};

// The labels of the workspaces on one monitor joined by two spaces, with where each label sits in the text.
WorkspaceLine JoinWorkspaces(const std::vector<Workspace>& workspaces, std::intptr_t monitor);

// The name of the workspace whose label covers a text position, or nullopt between labels.
std::optional<std::string> WorkspaceAt(const WorkspaceLine& line, std::uint32_t textPosition);
