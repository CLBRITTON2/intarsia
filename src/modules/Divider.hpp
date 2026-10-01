#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"

#include <cstdint>
#include <optional>
#include <string>

// A line across the bar between modules, such as between workspaces and window icons, in its color, width and height.
// Its width is its thickness along the bar and its height its share of the box across it, so a vertical bar's divider
// lies flat.
class Divider final : public IModule {
  public:
    explicit Divider(ModuleStyle style);

    // Changes only when the box it fills does.
    bool        Layout(const RenderContext& render) override;
    D2D1_SIZE_F Size() const override;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    // None: a divider takes no actions.
    const ModuleActions& Actions() const override;

  private:
    ModuleStyle _style;
    float       _boxCrossDips = 0.0f;
    Axis        _axis         = Axis::Horizontal;
    bool        _laidOut      = false;
};
