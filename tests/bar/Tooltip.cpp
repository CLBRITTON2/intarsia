#include "bar/Tooltip.hpp"

#include "support/Rect.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    constexpr RECT Rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
        return RECT{
            .left   = left,
            .top    = top,
            .right  = right,
            .bottom = bottom,
        };
    }

    constexpr SIZE kTooltipSize{.cx = 200, .cy = 30};
    constexpr RECT kMonitor    = Rect(0, 0, 1920, 1080);
    constexpr RECT kTopBarRect = Rect(0, 0, 1920, 40);

} // namespace

TEST(Tooltip, SitsBesideTheBarCenteredOnTheModuleAndStaysOnTheMonitor) {
    struct Case {
        std::string_view name;
        Edge             edge;
        RECT             barRect;
        RECT             moduleRect;
        RECT             expected;
    };
    const std::vector<Case> cases{
        {
            .name       = "a top bar's hangs below it",
            .edge       = Edge::Top,
            .barRect    = kTopBarRect,
            .moduleRect = Rect(900, 5, 1000, 35),
            .expected   = Rect(850, 40, 1050, 70),
        },
        {
            .name       = "a bottom bar's sits above it",
            .edge       = Edge::Bottom,
            .barRect    = Rect(0, 1040, 1920, 1080),
            .moduleRect = Rect(900, 1045, 1000, 1075),
            .expected   = Rect(850, 1010, 1050, 1040),
        },
        {
            .name       = "a left bar's sits right of it",
            .edge       = Edge::Left,
            .barRect    = Rect(0, 0, 40, 1080),
            .moduleRect = Rect(5, 500, 35, 580),
            .expected   = Rect(40, 525, 240, 555),
        },
        {
            .name       = "a right bar's sits left of it",
            .edge       = Edge::Right,
            .barRect    = Rect(1880, 0, 1920, 1080),
            .moduleRect = Rect(1885, 500, 1915, 580),
            .expected   = Rect(1680, 525, 1880, 555),
        },
        {
            .name       = "a side one too wide for its side",
            .edge       = Edge::Left,
            .barRect    = Rect(1500, 0, 1800, 1080),
            .moduleRect = Rect(1505, 500, 1795, 580),
            .expected   = Rect(1720, 525, 1920, 555),
        },
        {
            .name       = "near the monitor's right edge",
            .edge       = Edge::Top,
            .barRect    = kTopBarRect,
            .moduleRect = Rect(1880, 5, 1915, 35),
            .expected   = Rect(1720, 40, 1920, 70),
        },
        {
            .name       = "near the monitor's left edge",
            .edge       = Edge::Top,
            .barRect    = kTopBarRect,
            .moduleRect = Rect(0, 5, 30, 35),
            .expected   = Rect(0, 40, 200, 70),
        },
    };
    for (const Case& test : cases) {
        SCOPED_TRACE(test.name);
        const TooltipAnchor anchor{
            .target      = test.moduleRect,
            .barRect     = test.barRect,
            .monitorRect = kMonitor,
            .edge        = test.edge,
            .dpi         = 96,
        };
        EXPECT_EQ(TooltipRect(anchor, kTooltipSize), test.expected);
    }
}
