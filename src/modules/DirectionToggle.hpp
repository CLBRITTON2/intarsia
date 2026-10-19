#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/TextModule.hpp"
#include "wm/IWindowManager.hpp"

#include <optional>
#include <string>

// The focused container's tiling direction as the config's text for it, and a click flips it. Empty while nothing
// focused has a direction.
class DirectionToggle final : public IModule {
  public:
    DirectionToggle(TilingDirectionConfig config, IWindowManager& windowManager);

    bool Layout(const RenderContext& render) override;
    D2D1_SIZE_F Size() const override;
    void Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    void Click(D2D1_POINT_2F point) override;
    // None: a left click toggles the direction.
    const ModuleActions& Actions() const override;

  private:
    TilingDirectionConfig _config;
    IWindowManager& _windowManager;
    TextLine _glyph;
};

// What the module shows for a direction.
std::wstring DirectionText(const TilingDirectionConfig& config, std::optional<TilingDirection> direction);
