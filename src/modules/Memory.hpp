#pragma once

#include "config/Config.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"

#include <cstdint>
#include <vector>

// Bytes as gibibytes (1024^3), the unit Task Manager calls GB.
double BytesToGibibytes(std::uint64_t bytes);

// The values a memory format can name: {used}, gibibytes in use as a double.
std::vector<Placeholder> MemoryPlaceholders(std::uint64_t usedBytes);

// Physical memory in use: format = "{used:.1f} GB".
class Memory final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that cannot render a double.
    explicit Memory(TextModuleConfig config);

  private:
    ModuleText Format(const std::string& spec) override;
};
