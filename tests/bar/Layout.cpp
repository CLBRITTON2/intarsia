#include "bar/Layout.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(Layout, EmptySectionsStayEmpty) {
    const Sections<float> offsets = ArrangeSections({.left = {}, .center = {}, .right = {}}, 1000.0f, 8.0f);
    EXPECT_TRUE(offsets.left.empty());
    EXPECT_TRUE(offsets.center.empty());
    EXPECT_TRUE(offsets.right.empty());
}

TEST(Layout, LeftPacksFromTheStartWithSpacing) {
    const Sections<float> offsets = ArrangeSections({.left = {100.0f, 50.0f}, .center = {}, .right = {}}, 1000.0f, 8.0f);
    EXPECT_EQ(offsets.left, (std::vector<float>{8.0f, 116.0f}));
}

TEST(Layout, RightEndsOneSpacingBeforeTheEnd) {
    const Sections<float> offsets = ArrangeSections({.left = {}, .center = {}, .right = {100.0f, 50.0f}}, 1000.0f, 8.0f);
    EXPECT_EQ(offsets.right, (std::vector<float>{834.0f, 942.0f}));
}

TEST(Layout, CenterIsCenteredOnTheWholeBar) {
    const Sections<float> offsets = ArrangeSections({.left = {300.0f}, .center = {100.0f, 100.0f}, .right = {}}, 1000.0f, 10.0f);
    EXPECT_EQ(offsets.center, (std::vector<float>{395.0f, 505.0f}));
}

TEST(Layout, ZeroSpacingPlacesModulesEdgeToEdge) {
    const Sections<float> offsets = ArrangeSections({.left = {10.0f, 20.0f}, .center = {}, .right = {10.0f}}, 100.0f, 0.0f);
    EXPECT_EQ(offsets.left, (std::vector<float>{0.0f, 10.0f}));
    EXPECT_EQ(offsets.right, (std::vector<float>{90.0f}));
}

TEST(Layout, CenterWiderThanTheBarOverflowsBothEndsEqually) {
    const Sections<float> offsets = ArrangeSections({.left = {}, .center = {200.0f}, .right = {}}, 100.0f, 0.0f);
    EXPECT_EQ(offsets.center, (std::vector<float>{-50.0f}));
}
