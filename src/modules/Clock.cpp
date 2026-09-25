#include "modules/Clock.hpp"

#include "platform/Text.hpp"

#include <utility>

namespace {

    std::chrono::zoned_time<std::chrono::seconds> LocalTime(std::chrono::system_clock::time_point now) {
        return {std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)};
    }

} // namespace

Clock::Clock(TextModuleConfig config) : TextModule(std::move(config)) {
    RequireFormats("clock", Settings().format, Settings().tooltip, LocalTime(std::chrono::system_clock::now()));
}

ModuleText Clock::Format(const std::string& spec) {
    return ModuleText{.text = ToWide(FormatLocalTime(spec, std::chrono::system_clock::now())), .alert = false};
}

std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now) {
    return FormatValue(format, LocalTime(now));
}
