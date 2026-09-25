#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"

#include <cstdint>

// Bytes as gibibytes (1024^3), the unit Task Manager calls GB.
double BytesToGibibytes(std::uint64_t bytes);

// Physical memory in use, formatted from gibibytes as a double: format = "{:.1f} GB".
class Memory final : public IModule {
  public:
    // Throws ConfigError when the format cannot render a double.
    explicit Memory(TextModuleConfig config);

    void                 Layout(const RenderContext& render) override;
    D2D1_SIZE_F          Size() const override;
    void                 Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT>  MillisecondsUntilChange() const override;
    void                 Click(D2D1_POINT_2F point) override;
    const ModuleActions& Actions() const override;

  private:
    TextModuleConfig config_;
    IconText         text_;
};
