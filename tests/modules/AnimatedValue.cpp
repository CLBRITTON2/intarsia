#include "modules/AnimatedValue.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace {

    constexpr Transition kLinear100{.duration = std::chrono::milliseconds(100), .timing = TimingFunction::Linear};

} // namespace

TEST(ColorFade, MixingWithTransparentKeepsTheOpaqueColorsHue) {
    const D2D1_COLOR_F mixed = MixColors(D2D1::ColorF(0, 0.0f), D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(mixed.r, 1.0f);
    EXPECT_FLOAT_EQ(mixed.g, 0.0f);
    EXPECT_FLOAT_EQ(mixed.a, 0.5f);
    EXPECT_FLOAT_EQ(MixColors(D2D1::ColorF(0, 0.0f), D2D1::ColorF(0, 0.0f), 0.5f).a, 0.0f);
}

TEST(ColorFade, FadesToItsTargetOverTheTransition) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ColorFade                                   fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(fade.Running(start));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start);
    EXPECT_TRUE(fade.Running(start + std::chrono::milliseconds(50)));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(50)).r, 0.5f, 0.001f);
    EXPECT_FALSE(fade.Running(start + std::chrono::milliseconds(100)));
    EXPECT_FLOAT_EQ(fade.Color(start + std::chrono::milliseconds(100)).r, 1.0f);
}

TEST(ColorFade, RetargetedMidwayItStartsFromWhereItIs) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ColorFade                                   fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start);
    // The same target again neither restarts nor stalls the fade.
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start + std::chrono::milliseconds(50));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(75)).r, 0.75f, 0.001f);
    fade.Retarget(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), kLinear100, start + std::chrono::milliseconds(75));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(125)).r, 0.375f, 0.001f);
}

TEST(ColorFade, WithoutATransitionItChangesAtOnce) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ColorFade                                   fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), Transition{.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease}, start);
    EXPECT_FALSE(fade.Running(start));
    EXPECT_FLOAT_EQ(fade.Color(start).r, 1.0f);
}

TEST(ShadowFade, AnAddedShadowFadesInInPlaceAndARemovedOneOut) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const InsetShadow                           line{.xDips = 0.0f, .yDips = 1.0f, .blurDips = 0.0f, .spreadDips = 0.0f, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f)};
    ShadowFade                                  fade({});
    fade.Retarget({line}, kLinear100, start);
    const std::vector<InsetShadow> halfway = fade.Shadows(start + std::chrono::milliseconds(50));
    ASSERT_EQ(halfway.size(), 1u);
    EXPECT_FLOAT_EQ(halfway[0].yDips, 1.0f);
    EXPECT_FLOAT_EQ(halfway[0].color.r, 1.0f);
    EXPECT_NEAR(halfway[0].color.a, 0.25f, 0.001f);
    EXPECT_EQ(fade.Shadows(start + std::chrono::milliseconds(100)).size(), 1u);

    fade.Retarget({}, kLinear100, start + std::chrono::milliseconds(100));
    EXPECT_NEAR(fade.Shadows(start + std::chrono::milliseconds(150))[0].color.a, 0.25f, 0.001f);
    EXPECT_TRUE(fade.Shadows(start + std::chrono::milliseconds(200)).empty());
}

TEST(ShadowFade, RetargetedMidwayItKeepsNoMoreShadowsThanEitherEndHolds) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const InsetShadow                           line{.xDips = 0.0f, .yDips = 1.0f, .blurDips = 0.0f, .spreadDips = 0.0f, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)};
    ShadowFade                                  fade({line});
    for (std::int64_t step = 0; step < 10; ++step) {
        fade.Retarget(step % 2 == 0 ? std::vector<InsetShadow>{} : std::vector<InsetShadow>{line}, kLinear100, start + std::chrono::milliseconds(10 * step));
    }
    EXPECT_EQ(fade.Shadows(start + std::chrono::milliseconds(95)).size(), 1u);
    // The same target again neither restarts nor stalls the fade.
    fade.Retarget({line}, kLinear100, start + std::chrono::milliseconds(95));
    EXPECT_FALSE(fade.Running(start + std::chrono::milliseconds(190)));
}

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
