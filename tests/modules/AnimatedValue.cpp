#include "modules/AnimatedValue.hpp"

#include <gtest/gtest.h>

TEST(AnimatedValue, EveryTimingFunctionRunsFromZeroToOne) {
    for (const TimingFunction timing : {TimingFunction::Linear, TimingFunction::Ease, TimingFunction::EaseIn, TimingFunction::EaseOut, TimingFunction::EaseInOut}) {
        EXPECT_NEAR(Eased(timing, 0.0f), 0.0f, 0.001f);
        EXPECT_NEAR(Eased(timing, 1.0f), 1.0f, 0.001f);
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
