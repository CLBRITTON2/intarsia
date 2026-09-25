#pragma once

#include "config/Config.hpp"
#include "modules/TextModule.hpp"

#include <cstdint>

// Bytes as gibibytes (1024^3), the unit Task Manager calls GB.
double BytesToGibibytes(std::uint64_t bytes);

// Physical memory in use, formatted from gibibytes as a double: format = "{:.1f} GB".
class Memory final : public TextModule {
  public:
    // Throws ConfigError when the format cannot render a double.
    explicit Memory(TextModuleConfig config);

  private:
    ModuleText Format(const std::string& spec) override;
};
