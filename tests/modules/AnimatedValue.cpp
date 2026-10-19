#include "modules/AnimatedValue.hpp"

#include "config/Style.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using std::chrono::milliseconds;

    constexpr Transition kLinear100{.duration = std::chrono::milliseconds(100), .timing = TimingFunction::Linear};

    const std::chrono::steady_clock::time_point kStart = std::chrono::steady_clock::time_point() + std::chrono::hours(1);

    const InsetShadow kLine{
        .xDips = 0.0f,
        .yDips = 1.0f,
        .blurDips = 0.0f,
        .spreadDips = 0.0f,
        .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f),
    };

    ModuleStyle GroupStyle(std::string_view blocks) {
        const StyleSheet sheet = ParseStyleSheet("bar {\n  font: Some Font\n  size: 15\n  color: #e0def4\n}\n" + std::string(blocks), "style.conf");
        return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), ModuleKind::Group, "layout", {});
    }

    ModuleStyle TurningStyle() {
        return GroupStyle("layout {\n  transition: width 100ms linear\n  icon {\n    rotate: -90\n    transition: rotate 200ms linear\n  }\n  expanded {\n    icon {\n      "
                          "rotate: 90\n    }\n  }\n}\n");
    }

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
    ColorFade fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(fade.Running(start));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start);
    EXPECT_TRUE(fade.Running(start + std::chrono::milliseconds(50)));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(50)).r, 0.5f, 0.001f);
    EXPECT_FALSE(fade.Running(start + std::chrono::milliseconds(100)));
    EXPECT_FLOAT_EQ(fade.Color(start + std::chrono::milliseconds(100)).r, 1.0f);
}

TEST(ColorFade, RetargetedMidwayItStartsFromWhereItIs) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ColorFade fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start);
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), kLinear100, start + std::chrono::milliseconds(50));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(75)).r, 0.75f, 0.001f);
    fade.Retarget(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), kLinear100, start + std::chrono::milliseconds(75));
    EXPECT_NEAR(fade.Color(start + std::chrono::milliseconds(125)).r, 0.375f, 0.001f);
}

TEST(AnimatedValue, WithoutATransitionAFadeAndAFoldChangeAtOnce) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ColorFade fade(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    fade.Retarget(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), Transition{.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease}, start);
    EXPECT_FALSE(fade.Running(start));
    EXPECT_FLOAT_EQ(fade.Color(start).r, 1.0f);

    Fold fold(GroupStyle(""));
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart).revealed, 1.0f);
    EXPECT_FALSE(fold.Running(kStart));
}

TEST(ShadowFade, AnAddedShadowFadesInInPlaceAndARemovedOneOut) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ShadowFade fade({});
    fade.Retarget({kLine}, kLinear100, start);
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
    ShadowFade fade({kLine});
    for (std::int64_t step = 0; step < 10; ++step) {
        const std::chrono::steady_clock::time_point at = start + std::chrono::milliseconds(10 * step);
        if (step % 2 == 0) {
            fade.Retarget({}, kLinear100, at);
        } else {
            fade.Retarget({kLine}, kLinear100, at);
        }
    }
    EXPECT_EQ(fade.Shadows(start + std::chrono::milliseconds(95)).size(), 1u);
    fade.Retarget({kLine}, kLinear100, start + std::chrono::milliseconds(95));
    EXPECT_FALSE(fade.Running(start + std::chrono::milliseconds(190)));
}

TEST(AnimatedValue, TimingFunctionsRunFromZeroToOneInTheirCssShapes) {
    for (const TimingFunction timing : {TimingFunction::Linear, TimingFunction::Ease, TimingFunction::EaseIn, TimingFunction::EaseOut, TimingFunction::EaseInOut}) {
        EXPECT_NEAR(Eased(timing, 0.0f), 0.0f, 0.001f);
        EXPECT_NEAR(Eased(timing, 1.0f), 1.0f, 0.001f);
    }
    EXPECT_NEAR(Eased(TimingFunction::Linear, 0.5f), 0.5f, 0.001f);
    EXPECT_NEAR(Eased(TimingFunction::EaseInOut, 0.5f), 0.5f, 0.001f);
    EXPECT_GT(Eased(TimingFunction::EaseOut, 0.25f), 0.35f);
    EXPECT_LT(Eased(TimingFunction::EaseIn, 0.25f), 0.15f);
    EXPECT_NEAR(Eased(TimingFunction::Ease, 0.5f), 0.8024f, 0.002f);
}

// A bar draws at its last refresh, which can come just before the click that turned the value.
TEST(AnimatedValue, ReadBeforeItTurnedItHoldsWhereItTurned) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    AnimatedValue value(0.0f);
    value.Retarget(1.0f, kLinear100, start);
    EXPECT_NEAR(value.Value(start - std::chrono::milliseconds(3)), 0.0f, 0.001f);
    value.Retarget(0.0f, kLinear100, start + std::chrono::milliseconds(50));
    EXPECT_NEAR(value.Value(start + std::chrono::milliseconds(47)), 0.5f, 0.001f);
    EXPECT_TRUE(value.Running(start + std::chrono::milliseconds(47)));
}

TEST(Fold, StartsFoldedThenUnfoldsOverTheWidthTransitionAndTurnsOverTheIcons) {
    Fold fold(TurningStyle());
    EXPECT_FLOAT_EQ(fold.At(kStart).revealed, 0.0f);
    EXPECT_FLOAT_EQ(fold.At(kStart).angleDegrees, -90.0f);
    EXPECT_FALSE(fold.Running(kStart));
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).revealed, 0.5f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).angleDegrees, -45.0f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).revealed, 1.0f);
    EXPECT_TRUE(fold.Running(kStart + milliseconds(150)));
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(200)).angleDegrees, 90.0f);
    EXPECT_FALSE(fold.Running(kStart + milliseconds(200)));
}

TEST(Fold, ToggledMidWayFoldsBackFromWhereItIs) {
    Fold fold(TurningStyle());
    fold.Toggle(kStart);
    fold.Toggle(kStart + milliseconds(50));
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(75)).revealed, 0.25f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).revealed, 0.0f);
}
