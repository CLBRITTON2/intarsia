#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <cstdint>
#include <string>

// A module that shows its icon and one formatted value, redrawn on each interval tick: clock, cpu, memory, custom. A
// subclass supplies only the value, formatted with whichever spec it is given: format on the bar, tooltip on hover.
class TextModule : public IModule {
  public:
    bool                         Layout(const RenderContext& render) final;
    D2D1_SIZE_F                  Size() const final;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const final;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) final;
    void                         RightClick(D2D1_POINT_2F point) final;
    void                         Scroll(ScrollDirection direction) final;
    bool                         Hover(std::optional<D2D1_POINT_2F> point) final;
    const ModuleActions&         Actions() const final;
    std::optional<std::wstring>  TooltipText() override;
    std::optional<D2D1_RECT_F>   TooltipItem() const final;

  protected:
    explicit TextModule(TextModuleConfig config);

    const TextModuleConfig& Settings() const;

    // The module's icon and its current value run through spec.
    virtual ModuleText Format(const std::string& spec) = 0;

  private:
    TextModuleConfig _config;
    TextLine         _line;
};
