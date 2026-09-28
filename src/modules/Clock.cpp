#include "modules/Clock.hpp"

#include "platform/Text.hpp"

#include <utility>

Clock::Clock(TextModuleConfig config) : TextModule(std::move(config), "clock", ClockPlaceholders(std::chrono::system_clock::now())) {}

ModuleText Clock::Format(const std::string& spec) {
    return ModuleText{.icon = Settings().icon, .text = ToWide(FormatPlaceholders(spec, ClockPlaceholders(std::chrono::system_clock::now()))), .alert = false};
}

std::vector<Placeholder> ClockPlaceholders(std::chrono::system_clock::time_point now) {
    return {{.name = "time", .value = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)}}};
}
