#include "modules/AnimatedValue.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

    using std::chrono::milliseconds;

    const std::chrono::steady_clock::time_point kStart = std::chrono::steady_clock::time_point() + std::chrono::hours(1);

    Transition                                  Over(std::int64_t ms, TimingFunction timing) {
        return Transition{.duration = milliseconds(ms), .timing = timing};
    }

} // namespace

TEST(AnimatedValue, EveryTimingFunctionRunsFromZeroToOne) {
    for (const TimingFunction timing : {TimingFunction::Linear, TimingFunction::Ease, TimingFunction::EaseIn, TimingFunction::EaseOut, TimingFunction::EaseInOut}) {
        EXPECT_NEAR(Eased(timing, 0.0f), 0.0f, 0.001f);
        EXPECT_NEAR(Eased(timing, 1.0f), 1.0f, 0.001f);
        EXPECT_NEAR(Eased(timing, -1.0f), 0.0f, 0.001f);
        EXPECT_NEAR(Eased(timing, 2.0f), 1.0f, 0.001f);
    }
}

TEST(AnimatedValue, TimingFunctionsHaveTheirCssShapes) {
    EXPECT_NEAR(Eased(TimingFunction::Linear, 0.5f), 0.5f, 0.001f);
    EXPECT_NEAR(Eased(TimingFunction::EaseInOut, 0.5f), 0.5f, 0.001f);
    // ease-out is fast first and ease-in slow first, by CSS's control points.
    EXPECT_GT(Eased(TimingFunction::EaseOut, 0.25f), 0.35f);
    EXPECT_LT(Eased(TimingFunction::EaseIn, 0.25f), 0.15f);
    EXPECT_NEAR(Eased(TimingFunction::Ease, 0.5f), 0.8024f, 0.002f);
}

TEST(AnimatedValue, AZeroDurationChangesAtOnce) {
    AnimatedValue value(0.0f);
    value.Retarget(1.0f, Over(0, TimingFunction::Ease), kStart);
    EXPECT_FLOAT_EQ(value.Value(kStart), 1.0f);
    EXPECT_FALSE(value.Running(kStart));
}

TEST(AnimatedValue, MovesOverItsDurationThenStops) {
    AnimatedValue value(0.0f);
    value.Retarget(1.0f, Over(400, TimingFunction::Linear), kStart);
    EXPECT_TRUE(value.Running(kStart));
    EXPECT_NEAR(value.Value(kStart + milliseconds(100)), 0.25f, 0.001f);
    EXPECT_TRUE(value.Running(kStart + milliseconds(399)));
    EXPECT_FALSE(value.Running(kStart + milliseconds(400)));
    EXPECT_FLOAT_EQ(value.Value(kStart + milliseconds(500)), 1.0f);
}

TEST(AnimatedValue, TurningBackMidwayStartsWhereItIsAndTakesTheShareLeft) {
    AnimatedValue value(0.0f);
    value.Retarget(1.0f, Over(400, TimingFunction::Linear), kStart);
    const auto turned = kStart + milliseconds(100);
    value.Retarget(0.0f, Over(400, TimingFunction::Linear), turned);
    EXPECT_NEAR(value.Value(turned), 0.25f, 0.001f);
    EXPECT_TRUE(value.Running(turned + milliseconds(99)));
    EXPECT_FALSE(value.Running(turned + milliseconds(100)));
    EXPECT_FLOAT_EQ(value.Value(turned + milliseconds(100)), 0.0f);
}
