#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/ItemRow.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The workspaces on this bar's monitor, one item each: focused, occupied (has windows) or empty, each in its style. A
// click focuses the workspace under it. While the window manager is unreachable it shows "<name> disconnected" in the
// module's alert color instead.
class Workspaces final : public IModule {
  public:
    Workspaces(IWindowManager& windowManager, HMONITOR monitor, ModuleStyle style);

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
    ModuleStyle     _style;
    // What the items show, once laid out, so an unchanged list lays out nothing.
    std::vector<Workspace> _shown;
    std::wstring           _disconnectedText;
    bool                   _laidOut = false;
    std::vector<IconText>  _labels; // one per shown workspace
    std::vector<ItemStyle> _states; // one per shown workspace
    ItemRow                _row;
    IconText               _disconnected;
};

// The workspaces on one monitor, in order.
std::vector<Workspace> WorkspacesOnMonitor(const std::vector<Workspace>& workspaces, std::intptr_t monitor);

// The style a workspace draws in: focused before occupied before empty.
const ItemStyle& WorkspaceState(const Workspace& workspace, const ModuleStyle& style);
