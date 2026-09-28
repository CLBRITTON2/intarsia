#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"

#include <cstdint>
#include <optional>
#include <string>

// A line across the bar between modules, such as between workspaces and window icons, in its color, width and height.
class Divider final : public IModule {
  public:
    explicit Divider(ModuleStyle style);

    // Changes only when the box it fills does.
    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    void                         RightClick(D2D1_POINT_2F point) override;
    void                         Scroll(ScrollDirection direction) override;
    bool                         Hover(std::optional<D2D1_POINT_2F> point) override;
    // None: a divider takes no actions.
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;
    std::optional<D2D1_RECT_F>  TooltipItem() const override;

  private:
    ModuleStyle _style;
    float       _boxHeightDips = 0.0f;
    bool        _laidOut       = false;
};
