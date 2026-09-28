#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/ItemRow.hpp"
#include "wm/IWindowManager.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Each active binding mode's name in the module's .item style, like Waybar's sway/mode. Empty while no mode
// is active.
class BindingModes final : public IModule {
  public:
    BindingModes(const IWindowManager& windowManager, ModuleStyle style);

    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    void                         Scroll(ScrollDirection direction) override;
    bool                         Hover(std::optional<D2D1_POINT_2F> point) override;
    // None: the module takes no actions.
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    std::optional<size_t> _hovered; // the item under the cursor
    const IWindowManager&     _windowManager;
    ModuleStyle               _style;
    std::vector<std::wstring> _shown;
    bool                      _laidOut = false;
    std::vector<IconText>     _labels;
    ItemRow                   _row{.boxes = {}, .size = {.width = 0.0f, .height = 0.0f}};
};
