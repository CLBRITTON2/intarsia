#include "modules/Clock.hpp"

#include "TestStyle.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(std::int64_t milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

} // namespace

TEST(Clock, FormatRendersLiteralTextAroundTheSpec) {
    const std::wstring text = FormatPlaceholders("at {time:%Y}!", ClockPlaceholders(AtMilliseconds(0)));
    EXPECT_TRUE(text.starts_with(L"at 19")) << text;
    EXPECT_TRUE(text.ends_with(L"!")) << text;
}

TEST(Clock, AnInvalidSpecFailsWhenTheModuleIsBuilt) {
    EXPECT_THROW(Clock(TextConfigWithFormat("{time:%Q}")), ConfigError);
    EXPECT_THROW(Clock(TextConfigWithFormat("{:%H:%M}")), ConfigError);
    EXPECT_NO_THROW(Clock(TextConfigWithFormat("{time:%H:%M}")));
}
