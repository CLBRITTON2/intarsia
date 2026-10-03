#include "modules/Fold.hpp"

#include "config/Style.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>

namespace {

    using std::chrono::milliseconds;

    const std::chrono::steady_clock::time_point kStart = std::chrono::steady_clock::time_point() + std::chrono::hours(1);

    ModuleStyle                                 GroupStyle(std::string_view blocks) {
        const StyleSheet sheet = ParseStyleSheet("bar:\n  font: Some Font\n  size: 15\n  color: #e0def4\n" + std::string(blocks), "style.conf");
        return ResolveModuleStyle(sheet, ResolveBarStyle(sheet), ModuleKind::Group, "layout", {});
    }

    ModuleStyle TurningStyle() {
        return GroupStyle("layout:\n  transition: width 100ms linear\n  icon:\n    rotate: -90\n    transition: rotate 200ms linear\n  expanded:\n    icon:\n      rotate: 90\n");
    }

} // namespace

TEST(Fold, StartsFoldedAtTheIconsRotation) {
    const Fold         fold(TurningStyle());
    const FoldPosition position = fold.At(kStart);
    EXPECT_FLOAT_EQ(position.revealed, 0.0f);
    EXPECT_FLOAT_EQ(position.angleDegrees, -90.0f);
    EXPECT_FALSE(fold.Running(kStart));
}

TEST(Fold, UnfoldsOverTheWidthTransitionAndTurnsOverTheIcons) {
    Fold fold(TurningStyle());
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).revealed, 0.5f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(50)).angleDegrees, -45.0f);
    // Unfolded, and still turning.
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).revealed, 1.0f);
    EXPECT_TRUE(fold.Running(kStart + milliseconds(150)));
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(200)).angleDegrees, 90.0f);
    EXPECT_FALSE(fold.Running(kStart + milliseconds(200)));
}

TEST(Fold, ToggledMidWayFoldsBackFromWhereItIs) {
    Fold fold(TurningStyle());
    fold.Toggle(kStart);
    fold.Toggle(kStart + milliseconds(50));
    // Half open, it folds over half the duration.
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(75)).revealed, 0.25f);
    EXPECT_FLOAT_EQ(fold.At(kStart + milliseconds(100)).revealed, 0.0f);
}

TEST(Fold, WithoutTransitionsChangesAtOnce) {
    Fold fold(GroupStyle(""));
    fold.Toggle(kStart);
    EXPECT_FLOAT_EQ(fold.At(kStart).revealed, 1.0f);
    EXPECT_FALSE(fold.Running(kStart));
}
