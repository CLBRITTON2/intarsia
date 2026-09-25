#include "modules/Clock.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <format>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(long long milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

    TextModuleConfig ClockWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .icon = L"", .interval = std::chrono::seconds(60), .actions = {}};
    }

} // namespace

TEST(Clock, FormatRendersLiteralTextAroundTheSpec) {
    const std::string text = FormatLocalTime("at {:%Y}!", AtMilliseconds(0));
    EXPECT_TRUE(text.starts_with("at 19")) << text;
    EXPECT_TRUE(text.ends_with("!")) << text;
}

TEST(Clock, FormatKeepsMinutesTwoDigitsWide) {
    EXPECT_EQ(FormatLocalTime("{:%M}", std::chrono::system_clock::now()).size(), 2u);
}

TEST(Clock, FormatThrowsOnAnInvalidSpec) {
    EXPECT_THROW(FormatLocalTime("{:%Q}", AtMilliseconds(0)), std::format_error);
}

TEST(Clock, AnInvalidSpecFailsWhenTheModuleIsBuilt) {
    EXPECT_THROW(Clock(ClockWithFormat("{:%Q}")), ConfigError);
    EXPECT_NO_THROW(Clock(ClockWithFormat("{:%H:%M}")));
}
