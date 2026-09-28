#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <cstdint>
#include <string>

// What a text module shows after its icon.
struct ModuleText {
    std::wstring text;
    bool         alert; // drawn in the alert color, for a value that needs attention such as a failed command

    bool         operator==(const ModuleText& other) const = default;
};

// A module that shows its icon and one formatted value, redrawn on each interval tick: clock, cpu, memory, custom. A
// subclass supplies only the value, formatted with whichever spec it is given: format on the bar, tooltip on hover.
class TextModule : public IModule {
  public:
    bool                         Layout(const RenderContext& render) final;
    D2D1_SIZE_F                  Size() const final;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const final;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) final;
    void                         Scroll(ScrollDirection direction) final;
    const ModuleActions&         Actions() const final;
    std::optional<std::wstring>  TooltipText() override;

  protected:
    explicit TextModule(TextModuleConfig config);

    const TextModuleConfig& Settings() const;

    // The module's current value run through spec.
    virtual ModuleText Format(const std::string& spec) = 0;

  private:
    TextModuleConfig _config;
    ModuleText       _shown; // what _text holds, once laid out
    bool             _laidOut = false;
    IconText         _text;
};
