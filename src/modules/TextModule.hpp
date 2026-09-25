#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <string>

// A module that shows its icon and one formatted value, redrawn on each interval tick: clock, cpu, memory. A subclass
// supplies only the value.
class TextModule : public IModule {
  public:
    void                 Layout(const RenderContext& render) final;
    D2D1_SIZE_F          Size() const final;
    void                 Draw(const RenderContext& render, D2D1_POINT_2F origin) const final;
    std::optional<UINT>  MillisecondsUntilChange() const final;
    void                 Click(D2D1_POINT_2F point) final;
    const ModuleActions& Actions() const final;

  protected:
    explicit TextModule(TextModuleConfig config);

    const TextModuleConfig& Settings() const;

    // The module's value run through its format, sampled now.
    virtual std::wstring Text() = 0;

  private:
    TextModuleConfig _config;
    IconText         _text;
};
