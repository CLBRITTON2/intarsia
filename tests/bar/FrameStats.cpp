#include "bar/FrameStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

using namespace std::chrono_literals;

namespace {

    const std::chrono::steady_clock::time_point kStart{std::chrono::seconds(100)};

} // namespace

TEST(FrameStats, TheFirstFrameStartsTheStatsWithNoInterval) {
    const FrameStats stats = WithFrame(std::nullopt, kStart, 2ms);
    EXPECT_EQ(stats.frames, 1u);
    EXPECT_EQ(stats.first, kStart);
    EXPECT_EQ(stats.last, kStart);
    EXPECT_EQ(stats.longestInterval, std::chrono::steady_clock::duration::zero());
    EXPECT_EQ(stats.longestPresent, 2ms);
}

TEST(FrameStats, KeepsTheLongestIntervalAndPresent) {
    const FrameStats first  = WithFrame(std::nullopt, kStart, 1ms);
    const FrameStats second = WithFrame(first, kStart + 16ms, 5ms);
    const FrameStats third  = WithFrame(second, kStart + 50ms, 2ms);
    EXPECT_EQ(third.frames, 3u);
    EXPECT_EQ(third.first, kStart);
    EXPECT_EQ(third.last, kStart + 50ms);
    EXPECT_EQ(third.longestInterval, 34ms);
    EXPECT_EQ(third.longestPresent, 5ms);
}
