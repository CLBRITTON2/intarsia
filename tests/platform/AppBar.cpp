#include "platform/AppBar.hpp"

#include <gtest/gtest.h>

namespace {

    bool SameRect(const RECT& actual, const RECT& expected) {
        return actual.left == expected.left && actual.top == expected.top && actual.right == expected.right && actual.bottom == expected.bottom;
    }

    const RECT kMonitor{.left = 0, .top = 0, .right = 1920, .bottom = 1080};

} // namespace

TEST(AppBar, DockedRectTakesAStripAlongEachEdge) {
    EXPECT_TRUE(SameRect(DockedRect(kMonitor, Edge::Top, 54), RECT{.left = 0, .top = 0, .right = 1920, .bottom = 54}));
    EXPECT_TRUE(SameRect(DockedRect(kMonitor, Edge::Bottom, 54), RECT{.left = 0, .top = 1026, .right = 1920, .bottom = 1080}));
    EXPECT_TRUE(SameRect(DockedRect(kMonitor, Edge::Left, 54), RECT{.left = 0, .top = 0, .right = 54, .bottom = 1080}));
    EXPECT_TRUE(SameRect(DockedRect(kMonitor, Edge::Right, 54), RECT{.left = 1866, .top = 0, .right = 1920, .bottom = 1080}));
}

TEST(AppBar, DockedRectKeepsASecondaryMonitorOffset) {
    const RECT secondary{.left = 1920, .top = -200, .right = 4480, .bottom = 1240};
    EXPECT_TRUE(SameRect(DockedRect(secondary, Edge::Top, 81), RECT{.left = 1920, .top = -200, .right = 4480, .bottom = -119}));
}

TEST(AppBar, DockedRectDoesNotClampAThicknessLargerThanTheMonitor) {
    EXPECT_TRUE(SameRect(DockedRect(kMonitor, Edge::Top, 2000), RECT{.left = 0, .top = 0, .right = 1920, .bottom = 2000}));
}
