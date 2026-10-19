#pragma once

#include "config/Config.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"

#include <chrono>
#include <string>
#include <vector>

// The local time as {time}, formatted by a std::format chrono spec: format = "{time:%a %d %b %Y, %H:%M}".
class Clock final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or its spec is not a valid chrono spec.
    explicit Clock(TextModuleConfig config);

  private:
    ModuleText Format(const std::string& spec) override;

    std::chrono::sys_seconds _formattedSecond;
    std::string _formattedSpec;
    std::wstring _formatted;
};

// The values a clock format can name at now: {time}, the local time to the second.
std::vector<Placeholder> ClockPlaceholders(std::chrono::system_clock::time_point now);
