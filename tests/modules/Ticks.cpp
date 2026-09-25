#include "modules/Ticks.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(long long milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

} // namespace

TEST(Ticks, WaitsAFullIntervalOnABoundary) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'000), std::chrono::seconds(60)), std::chrono::milliseconds(60'000));
}

TEST(Ticks, WaitsUntilTheNextBoundaryJustAfterOne) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'001), std::chrono::seconds(60)), std::chrono::milliseconds(59'999));
}

TEST(Ticks, AlignsToEachInterval) {
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(1'250), std::chrono::seconds(1)), std::chrono::milliseconds(750));
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(3'599'000), std::chrono::seconds(3600)), std::chrono::milliseconds(1'000));
}
