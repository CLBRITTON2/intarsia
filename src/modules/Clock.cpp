#include "modules/Clock.hpp"

#include <utility>

Clock::Clock(TextModuleConfig config) : TextModule(std::move(config), "clock", ClockPlaceholders(std::chrono::system_clock::now())) {}

// Formats at most once per second per spec: the bar also lays the clock out for other modules' changes and every
// animation frame, and each format looks up the time zone. Hovering swaps the spec, so the tooltip costs a reformat.
ModuleText Clock::Format(const std::string& spec) {
    const std::chrono::sys_seconds second = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    if (second != _formattedSecond || spec != _formattedSpec) {
        _formatted       = FormatPlaceholders(spec, ClockPlaceholders(second));
        _formattedSecond = second;
        _formattedSpec   = spec;
    }
    return NormalText(Settings().icon, _formatted);
}

std::vector<Placeholder> ClockPlaceholders(std::chrono::system_clock::time_point now) {
    return {{.name = "time", .value = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)}}};
}
