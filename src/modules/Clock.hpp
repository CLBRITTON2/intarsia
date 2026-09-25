#pragma once

#include "config/Config.hpp"
#include "modules/TextModule.hpp"

#include <chrono>
#include <string>

// The local time, formatted from a zoned_time: format = "{:%a %d %b %Y, %H:%M}".
class Clock final : public TextModule {
  public:
    // Throws ConfigError when the format is not a valid std::format chrono spec.
    explicit Clock(TextModuleConfig config);

  private:
    std::wstring Text() override;
};

// Local time rendered with format, a std::format spec such as "{:%a %d %b %Y, %H:%M}".
std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now);
