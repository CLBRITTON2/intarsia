#pragma once

#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "wm/IWindowManager.hpp"

#include <optional>
#include <string>
#include <vector>

// The active binding modes' names in the accent color, like Waybar's sway/mode. Empty while no mode is active.
class BindingModes final : public IModule {
  public:
    explicit BindingModes(const IWindowManager& windowManager);

    bool                Layout(const RenderContext& render) override;
    D2D1_SIZE_F         Size() const override;
    void                Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT> MillisecondsUntilChange() const override;
    void                Click(D2D1_POINT_2F point) override;
    // None: the module has no config table.
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    const IWindowManager& _windowManager;
    TextLine              _line;
};

// Mode names joined by two spaces, as workspace labels are.
std::wstring JoinModes(const std::vector<std::wstring>& modes);
