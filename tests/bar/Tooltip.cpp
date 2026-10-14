#include "bar/Tooltip.hpp"

#include "support/Rect.hpp"

#include <gtest/gtest.h>

namespace {

    constexpr RECT kMonitor{0, 0, 1920, 1080};

    TooltipAnchor  AnchorAt(Edge edge, RECT barRect, RECT target) {
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
    const RECT rect = TooltipRect(AnchorAt(Edge::Top, RECT{0, 0, 1920, 40}, RECT{900, 5, 1000, 35}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(rect, (RECT{850, 40, 1050, 70}));
}

TEST(Tooltip, BottomBarTooltipSitsAboveTheBar) {
    const RECT rect = TooltipRect(AnchorAt(Edge::Bottom, RECT{0, 1040, 1920, 1080}, RECT{900, 1045, 1000, 1075}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(rect, (RECT{850, 1010, 1050, 1040}));
}

TEST(Tooltip, SideBarsPlaceTheTooltipBesideTheBar) {
    const RECT left = TooltipRect(AnchorAt(Edge::Left, RECT{0, 0, 40, 1080}, RECT{5, 500, 35, 580}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(left, (RECT{40, 525, 240, 555}));
    const RECT right = TooltipRect(AnchorAt(Edge::Right, RECT{1880, 0, 1920, 1080}, RECT{1885, 500, 1915, 580}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(right, (RECT{1680, 525, 1880, 555}));
}

TEST(Tooltip, ASideTooltipTooWideForItsSideStaysOnTheMonitor) {
    const RECT rect = TooltipRect(AnchorAt(Edge::Left, RECT{1500, 0, 1800, 1080}, RECT{1505, 500, 1795, 580}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(rect, (RECT{1720, 525, 1920, 555}));
}

TEST(Tooltip, ATooltipNearTheMonitorEdgeStaysOnTheMonitor) {
    const RECT nearRight = TooltipRect(AnchorAt(Edge::Top, RECT{0, 0, 1920, 40}, RECT{1880, 5, 1915, 35}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(nearRight, (RECT{1720, 40, 1920, 70}));
    const RECT nearLeft = TooltipRect(AnchorAt(Edge::Top, RECT{0, 0, 1920, 40}, RECT{0, 5, 30, 35}), SIZE{.cx = 200, .cy = 30});
    EXPECT_EQ(nearLeft, (RECT{0, 40, 200, 70}));
}
