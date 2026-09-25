#include "modules/Clock.hpp"

#include "modules/Ticks.hpp"
#include "platform/Text.hpp"

#include <utility>

namespace {

    std::chrono::zoned_time<std::chrono::seconds> LocalTime(std::chrono::system_clock::time_point now) {
        return {std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(now)};
    }

} // namespace

Clock::Clock(TextModuleConfig config) : config_(std::move(config)), text_{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {
    RequireFormat("clock.format", config_.format, LocalTime(std::chrono::system_clock::now()));
}

void Clock::Layout(const RenderContext& render) {
    text_ = LayOutIconText(render, config_.icon, ToWide(FormatLocalTime(config_.format, std::chrono::system_clock::now())));
}

D2D1_SIZE_F Clock::Size() const {
    return text_.size;
}

void Clock::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, text_.layout.Get(), render.foreground);
}

std::optional<UINT> Clock::MillisecondsUntilChange() const {
    return static_cast<UINT>(TimeUntilNextTick(std::chrono::system_clock::now(), config_.interval).count());
}

void                 Clock::Click(D2D1_POINT_2F) {}

const ModuleActions& Clock::Actions() const {
    return config_.actions;
}

std::string FormatLocalTime(const std::string& format, std::chrono::system_clock::time_point now) {
    return FormatValue(format, LocalTime(now));
}
