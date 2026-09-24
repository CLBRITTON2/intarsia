#pragma once

#include "modules/IModule.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

// The workspaces on this bar's monitor, the focused one in the accent color. While the window manager is unreachable
// it shows "<name> disconnected" in the alert color instead.
class Workspaces final : public IModule {
  public:
    Workspaces(const IWindowManager& windowManager, HMONITOR monitor);

    void                Layout(const RenderContext& render) override;
    D2D1_SIZE_F         Size() const override;
    void                Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT> MillisecondsUntilChange() const override;

  private:
    const IWindowManager&                     windowManager_;
    std::intptr_t                             monitor_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout_;
    D2D1_SIZE_F                               size_;
};

struct LabelRun {
    UINT32 start;
    UINT32 length;
    bool   focused;
};

struct WorkspaceLine {
    std::wstring          text;
    std::vector<LabelRun> runs;
};

// The labels of the workspaces on one monitor joined by two spaces, with where each label sits in the text.
WorkspaceLine JoinWorkspaces(const std::vector<Workspace>& workspaces, std::intptr_t monitor);
