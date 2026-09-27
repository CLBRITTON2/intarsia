#include "bar/Layout.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(Layout, EmptySectionsHaveNoLength) {
    const BarLayout layout = ArrangeBar({.left = {}, .center = {}, .right = {}}, 1000.0f, 5.0f, 4.0f);
    EXPECT_EQ(layout.left.length, 0.0f);
    EXPECT_EQ(layout.center.length, 0.0f);
    EXPECT_EQ(layout.right.length, 0.0f);
    EXPECT_TRUE(layout.left.offsets.empty());
}

TEST(Layout, LeftStartsOneMarginInAndPadsItsModules) {
    const BarLayout layout = ArrangeBar({.left = {100.0f, 50.0f}, .center = {}, .right = {}}, 1000.0f, 5.0f, 4.0f);
    EXPECT_EQ(layout.left.start, 5.0f);
    EXPECT_EQ(layout.left.length, 158.0f);
    EXPECT_EQ(layout.left.offsets, (std::vector<float>{9.0f, 109.0f}));
}

TEST(Layout, RightEndsOneMarginBeforeTheEnd) {
    const BarLayout layout = ArrangeBar({.left = {}, .center = {}, .right = {100.0f, 50.0f}}, 1000.0f, 5.0f, 4.0f);
    EXPECT_EQ(layout.right.start + layout.right.length, 995.0f);
    EXPECT_EQ(layout.right.offsets, (std::vector<float>{841.0f, 941.0f}));
}

TEST(Layout, CenterIsCenteredOnTheWholeBar) {
    const BarLayout layout = ArrangeBar({.left = {300.0f}, .center = {100.0f, 100.0f}, .right = {}}, 1000.0f, 5.0f, 10.0f);
    EXPECT_EQ(layout.center.start, 390.0f);
    EXPECT_EQ(layout.center.offsets, (std::vector<float>{400.0f, 500.0f}));
}

TEST(Layout, CenterWiderThanTheBarOverflowsBothEndsEqually) {
    const BarLayout layout = ArrangeBar({.left = {}, .center = {200.0f}, .right = {}}, 100.0f, 0.0f, 0.0f);
    EXPECT_EQ(layout.center.offsets, (std::vector<float>{-50.0f}));
}
