#include "modules/Clock.hpp"

#include "platform/Text.hpp"

#include <utility>

namespace {

    std::chrono::zoned_time<std::chrono::seconds> LocalTime(std::chrono::system_clock::time_point now) {
        return {std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)};
    }

} // namespace

Clock::Clock(TextModuleConfig config) : TextModule(std::move(config)) {
    RequireFormat("clock format", Settings().format, LocalTime(std::chrono::system_clock::now()));
}

std::wstring Clock::Text() {
    return ToWide(FormatLocalTime(Settings().format, std::chrono::system_clock::now()));
}

std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now) {
    return FormatValue(format, LocalTime(now));
}
