#include "bar/FrameStats.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace {

    using std::chrono::milliseconds;

    const std::chrono::steady_clock::time_point kStart{};

} // namespace

TEST(FrameStats, OnePresentShownPerRefreshDropsAndRepeatsNothing) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), 10, ShownPresent{.present = 10, .refresh = 100});
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 11, ShownPresent{.present = 11, .refresh = 101});
    stats                           = WithFrame(stats, kStart + milliseconds(8), milliseconds(1), 12, ShownPresent{.present = 12, .refresh = 102});
    EXPECT_EQ(stats->frames, 3u);
    EXPECT_EQ(stats->droppedFrames, 0u);
    EXPECT_EQ(stats->repeatedRefreshes, 0u);
}

TEST(FrameStats, ARefreshShowingTheSameFrameThenAJumpPastOneCountsARepeatAndADrop) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), 10, ShownPresent{.present = 10, .refresh = 100});
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 11, ShownPresent{.present = 10, .refresh = 101});
    stats                           = WithFrame(stats, kStart + milliseconds(8), milliseconds(1), 12, ShownPresent{.present = 12, .refresh = 102});
    EXPECT_EQ(stats->repeatedRefreshes, 1u);
    EXPECT_EQ(stats->droppedFrames, 1u);
}

TEST(FrameStats, TheIdleRefreshesBeforeTheAnimationCountNothing) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), 11, ShownPresent{.present = 10, .refresh = 100});
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 12, ShownPresent{.present = 11, .refresh = 300});
    stats                           = WithFrame(stats, kStart + milliseconds(8), milliseconds(1), 13, ShownPresent{.present = 12, .refresh = 301});
    EXPECT_EQ(stats->droppedFrames, 0u);
    EXPECT_EQ(stats->repeatedRefreshes, 0u);
}

TEST(FrameStats, NoStatisticsCountNothingAndTheNextReadingStartsAfresh) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), 10, std::nullopt);
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 11, ShownPresent{.present = 50, .refresh = 900});
    stats                           = WithFrame(stats, kStart + milliseconds(8), milliseconds(1), 12, std::nullopt);
    stats                           = WithFrame(stats, kStart + milliseconds(12), milliseconds(1), 13, ShownPresent{.present = 10, .refresh = 100});
    EXPECT_EQ(stats->droppedFrames, 0u);
    EXPECT_EQ(stats->repeatedRefreshes, 0u);
}

TEST(FrameStats, CountersThatWrapAroundStillCountOneRefreshApart) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), UINT32_MAX, ShownPresent{.present = UINT32_MAX, .refresh = UINT32_MAX});
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 0, ShownPresent{.present = 1, .refresh = 0});
    EXPECT_EQ(stats->droppedFrames, 1u);
    EXPECT_EQ(stats->repeatedRefreshes, 0u);
}

TEST(FrameStats, APresentFromBeforeTheAnimationAcrossTheWraparoundCountsNothing) {
    std::optional<FrameStats> stats = WithFrame(std::nullopt, kStart, milliseconds(1), 0, ShownPresent{.present = UINT32_MAX, .refresh = 100});
    stats                           = WithFrame(stats, kStart + milliseconds(4), milliseconds(1), 1, ShownPresent{.present = 0, .refresh = 300});
    EXPECT_EQ(stats->droppedFrames, 0u);
    EXPECT_EQ(stats->repeatedRefreshes, 0u);
}
