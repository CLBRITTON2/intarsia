#include "modules/Clock.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <format>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(long long milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

} // namespace

TEST(Clock, TickWaitsAFullIntervalOnABoundary) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'000), std::chrono::seconds(60)), std::chrono::milliseconds(60'000));
}

TEST(Clock, TickWaitsUntilTheNextBoundaryJustAfterOne) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'001), std::chrono::seconds(60)), std::chrono::milliseconds(59'999));
}

TEST(Clock, TickAlignsToEachInterval) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(1'250), std::chrono::seconds(1)), std::chrono::milliseconds(750));
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(3'599'000), std::chrono::seconds(3600)), std::chrono::milliseconds(1'000));
}

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
