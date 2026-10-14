#include "bar/Tooltip.hpp"

#include "support/Rect.hpp"

#include <gtest/gtest.h>

namespace {

    constexpr SIZE kTooltipSize{.cx = 200, .cy = 30};
    constexpr RECT kMonitor{
        .left   = 0,
        .top    = 0,
        .right  = 1920,
        .bottom = 1080,
    };
    constexpr RECT kTopBarRect{
        .left   = 0,
        .top    = 0,
        .right  = 1920,
        .bottom = 40,
    };

    TooltipAnchor AnchorAt(Edge edge, RECT barRect, RECT target) {
        return TooltipAnchor{
            .target      = target,
            .barRect     = barRect,
            .monitorRect = kMonitor,
            .edge        = edge,
            .dpi         = 96,
        };
    }

} // namespace

TEST(Tooltip, TopBarTooltipHangsBelowTheBarCenteredOnTheModule) {
    const RECT moduleRect{
        .left   = 900,
        .top    = 5,
        .right  = 1000,
        .bottom = 35,
    };
    const RECT expected{
        .left   = 850,
        .top    = 40,
        .right  = 1050,
        .bottom = 70,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Top, kTopBarRect, moduleRect), kTooltipSize), expected);
}

TEST(Tooltip, BottomBarTooltipSitsAboveTheBar) {
    const RECT barRect{
        .left   = 0,
        .top    = 1040,
        .right  = 1920,
        .bottom = 1080,
    };
    const RECT moduleRect{
        .left   = 900,
        .top    = 1045,
        .right  = 1000,
        .bottom = 1075,
    };
    const RECT expected{
        .left   = 850,
        .top    = 1010,
        .right  = 1050,
        .bottom = 1040,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Bottom, barRect, moduleRect), kTooltipSize), expected);
}

TEST(Tooltip, SideBarsPlaceTheTooltipBesideTheBar) {
    const RECT leftBarRect{
        .left   = 0,
        .top    = 0,
        .right  = 40,
        .bottom = 1080,
    };
    const RECT leftModuleRect{
        .left   = 5,
        .top    = 500,
        .right  = 35,
        .bottom = 580,
    };
    const RECT expectedLeft{
        .left   = 40,
        .top    = 525,
        .right  = 240,
        .bottom = 555,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Left, leftBarRect, leftModuleRect), kTooltipSize), expectedLeft);

    const RECT rightBarRect{
        .left   = 1880,
        .top    = 0,
        .right  = 1920,
        .bottom = 1080,
    };
    const RECT rightModuleRect{
        .left   = 1885,
        .top    = 500,
        .right  = 1915,
        .bottom = 580,
    };
    const RECT expectedRight{
        .left   = 1680,
        .top    = 525,
        .right  = 1880,
        .bottom = 555,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Right, rightBarRect, rightModuleRect), kTooltipSize), expectedRight);
}

TEST(Tooltip, ASideTooltipTooWideForItsSideStaysOnTheMonitor) {
    const RECT barRect{
        .left   = 1500,
        .top    = 0,
        .right  = 1800,
        .bottom = 1080,
    };
    const RECT moduleRect{
        .left   = 1505,
        .top    = 500,
        .right  = 1795,
        .bottom = 580,
    };
    const RECT expected{
        .left   = 1720,
        .top    = 525,
        .right  = 1920,
        .bottom = 555,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Left, barRect, moduleRect), kTooltipSize), expected);
}

TEST(Tooltip, ATooltipNearTheMonitorEdgeStaysOnTheMonitor) {
    const RECT nearRightModuleRect{
        .left   = 1880,
        .top    = 5,
        .right  = 1915,
        .bottom = 35,
    };
    const RECT expectedNearRight{
        .left   = 1720,
        .top    = 40,
        .right  = 1920,
        .bottom = 70,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Top, kTopBarRect, nearRightModuleRect), kTooltipSize), expectedNearRight);

    const RECT nearLeftModuleRect{
        .left   = 0,
        .top    = 5,
        .right  = 30,
        .bottom = 35,
    };
    const RECT expectedNearLeft{
        .left   = 0,
        .top    = 40,
        .right  = 200,
        .bottom = 70,
    };
    EXPECT_EQ(TooltipRect(AnchorAt(Edge::Top, kTopBarRect, nearLeftModuleRect), kTooltipSize), expectedNearLeft);
}
