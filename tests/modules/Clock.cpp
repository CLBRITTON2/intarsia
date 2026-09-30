#include "modules/Clock.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <chrono>
#include <format>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(std::int64_t milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

    TextModuleConfig ClockWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .tooltip = std::nullopt, .icon = L"", .interval = std::chrono::seconds(60), .actions = {}};
    }

} // namespace

TEST(Clock, FormatRendersLiteralTextAroundTheSpec) {
    const std::string text = FormatPlaceholders("at {time:%Y}!", ClockPlaceholders(AtMilliseconds(0)));
    EXPECT_TRUE(text.starts_with("at 19")) << text;
    EXPECT_TRUE(text.ends_with("!")) << text;
}

TEST(Clock, AnInvalidSpecFailsWhenTheModuleIsBuilt) {
    EXPECT_THROW(Clock(ClockWithFormat("{time:%Q}")), ConfigError);
    EXPECT_THROW(Clock(ClockWithFormat("{:%H:%M}")), ConfigError);
    EXPECT_NO_THROW(Clock(ClockWithFormat("{time:%H:%M}")));
}
