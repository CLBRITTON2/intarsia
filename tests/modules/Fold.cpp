#include "modules/Fold.hpp"

#include "config/Style.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>

namespace {

    using std::chrono::milliseconds;

    const std::chrono::steady_clock::time_point kStart = std::chrono::steady_clock::time_point() + std::chrono::hours(1);

    ModuleStyle                                 GroupStyle(std::string_view rules) {
        const StyleSheet sheet = ParseStyleSheet(std::string(R"(* { font-family: "Some Font"; font-size: 15px; color: #e0def4; } )") + std::string(rules), "style.css");
        return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), ModuleKind::Group, "layout", {});
    }

    ModuleStyle TurningStyle() {
        return GroupStyle("group { transition: width 100ms linear; } group .icon { transform: rotate(-90deg); transition: transform 200ms linear; } "
                          "group.expanded .icon { transform: rotate(90deg); }");
    }

} // namespace

TEST(Fold, StartsFoldedAtTheIconsRotation) {
    const Fold         fold(TurningStyle());
    const FoldPosition position = fold.At(kStart);
    EXPECT_FLOAT_EQ(position.reveal, 0.0f);
    EXPECT_FLOAT_EQ(position.angleDegrees, -90.0f);
    EXPECT_FALSE(fold.Running(kStart));
}

TEST(Fold, UnfoldsOverTheWidthTransitionAndTurnsOverTheIcons) {
    Fold fold(TurningStyle());
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).reveal, 0.5f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).angleDegrees, -45.0f);
    // Unfolded, and still turning.
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).reveal, 1.0f);
    EXPECT_TRUE(fold.Running(kStart + milliseconds(150)));
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(200)).angleDegrees, 90.0f);
    EXPECT_FALSE(fold.Running(kStart + milliseconds(200)));
}

TEST(Fold, ToggledMidWayFoldsBackFromWhereItIs) {
    Fold fold(TurningStyle());
    fold.Toggle(kStart);
    fold.Toggle(kStart + milliseconds(50));
    // Half open, it folds over half the duration.
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(75)).reveal, 0.25f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).reveal, 0.0f);
}

TEST(Fold, WithoutTransitionsChangesAtOnce) {
    Fold fold(GroupStyle(""));
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart).reveal, 1.0f);
    EXPECT_FALSE(fold.Running(kStart));
}
