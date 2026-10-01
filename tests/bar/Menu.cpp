#include "bar/Menu.hpp"
#include "bar/Tooltip.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace {

    MenuRow RowAt(float top, std::optional<size_t> entry) {
        return MenuRow{.text = nullptr, .box = D2D1::RectF(4, top, 104, top + 20), .textOrigin = D2D1::Point2F(0, 0), .style = nullptr, .entry = entry};
    }

    // A 100 DIP viewport over 400 DIPs of rows, beside a track as tall as the viewport.
    MenuScroll ScrollAt(float offsetDips) {
        return MenuScroll{.viewport = D2D1::RectF(4, 4, 104, 104), .contentDips = 400.0f, .offsetDips = offsetDips};
    }

    const D2D1_RECT_F kTrack = D2D1::RectF(106, 4, 110, 104);

    D2D1_RECT_F       ThumbOrFail(const MenuScroll& scroll) {
        const std::optional<D2D1_RECT_F> thumb = ThumbRect(scroll, kTrack);
        if (!thumb.has_value()) {
            ADD_FAILURE() << "no thumb";
            return D2D1::RectF(0, 0, 0, 0);
        }
        return *thumb;
    }

} // namespace

TEST(Menu, EntryAtFindsTheRowUnderThePoint) {
    const std::vector<MenuRow> rows{RowAt(4, 0), RowAt(28, 1)};
    EXPECT_EQ(EntryAt(rows, ScrollAt(0), D2D1::Point2F(50, 10)), 0u);
    EXPECT_EQ(EntryAt(rows, ScrollAt(0), D2D1::Point2F(50, 30)), 1u);
    // Between rows and in the menu's padding there is nothing to pick.
    EXPECT_EQ(EntryAt(rows, ScrollAt(0), D2D1::Point2F(50, 25)), std::nullopt);
    EXPECT_EQ(EntryAt(rows, ScrollAt(0), D2D1::Point2F(2, 10)), std::nullopt);
}

TEST(Menu, EntryAtFollowsTheScrolledRows) {
    const std::vector<MenuRow> rows{RowAt(4, 0), RowAt(28, 1), RowAt(110, 2)};
    EXPECT_EQ(EntryAt(rows, ScrollAt(24), D2D1::Point2F(50, 10)), 1u);
    EXPECT_EQ(EntryAt(rows, ScrollAt(100), D2D1::Point2F(50, 12)), 2u);
    // A row scrolled up into the padding above the viewport cannot be picked there.
    EXPECT_EQ(EntryAt(rows, ScrollAt(24), D2D1::Point2F(50, 2)), std::nullopt);
}

TEST(Menu, AFailuresRowCannotBePicked) {
    EXPECT_EQ(EntryAt({RowAt(4, std::nullopt)}, ScrollAt(0), D2D1::Point2F(50, 10)), std::nullopt);
}

TEST(Menu, AnEntrysTooltipGoesUnderItsRowOrAboveItNearTheMonitorsBottom) {
    constexpr RECT kMonitor{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
    constexpr SIZE kSize{.cx = 100, .cy = 30};
    const RECT     below = TooltipRect(RowTooltipAnchor(RECT{.left = 1600, .top = 300, .right = 1800, .bottom = 320}, kMonitor, 96), kSize);
    EXPECT_EQ(below.left, 1650);
    EXPECT_EQ(below.top, 320);
    const RECT above = TooltipRect(RowTooltipAnchor(RECT{.left = 1600, .top = 900, .right = 1800, .bottom = 920}, kMonitor, 96), kSize);
    EXPECT_EQ(above.top, 870);
    // A tooltip wider than its row near the monitor's edge stays on the monitor.
    const RECT wide = TooltipRect(RowTooltipAnchor(RECT{.left = 1700, .top = 300, .right = 1900, .bottom = 320}, kMonitor, 96), SIZE{.cx = 400, .cy = 30});
    EXPECT_EQ(wide.right, 1920);
}

TEST(Menu, ArrowKeysStartAtEitherEndAndWrap) {
    EXPECT_EQ(StepHighlight(std::nullopt, 3, MenuStep::Down), 0u);
    EXPECT_EQ(StepHighlight(std::nullopt, 3, MenuStep::Up), 2u);
    EXPECT_EQ(StepHighlight(1, 3, MenuStep::Down), 2u);
    EXPECT_EQ(StepHighlight(2, 3, MenuStep::Down), 0u);
    EXPECT_EQ(StepHighlight(0, 3, MenuStep::Up), 2u);
    EXPECT_EQ(StepHighlight(std::nullopt, 0, MenuStep::Down), std::nullopt);
}

TEST(Menu, ScrollingStopsAtTheFirstAndLastRows) {
    EXPECT_FLOAT_EQ(ScrolledTo(ScrollAt(0), -20).offsetDips, 0.0f);
    EXPECT_FLOAT_EQ(ScrolledTo(ScrollAt(0), 150).offsetDips, 150.0f);
    EXPECT_FLOAT_EQ(ScrolledTo(ScrollAt(0), 500).offsetDips, 300.0f);
    const MenuScroll fits{.viewport = D2D1::RectF(4, 4, 104, 104), .contentDips = 80.0f, .offsetDips = 0.0f};
    EXPECT_FLOAT_EQ(ScrolledTo(fits, 30).offsetDips, 0.0f);
}

TEST(Menu, ScrollingToShowARowMovesTheLeast) {
    // Below the viewport: its bottom comes to the viewport's bottom.
    EXPECT_FLOAT_EQ(ScrolledToShow(ScrollAt(0), D2D1::RectF(4, 120, 104, 140)).offsetDips, 36.0f);
    // Above it: its top comes to the viewport's top.
    EXPECT_FLOAT_EQ(ScrolledToShow(ScrollAt(100), D2D1::RectF(4, 54, 104, 74)).offsetDips, 50.0f);
    // Already shown: nothing moves.
    EXPECT_FLOAT_EQ(ScrolledToShow(ScrollAt(100), D2D1::RectF(4, 120, 104, 140)).offsetDips, 100.0f);
    // Taller than the viewport: its top shows.
    EXPECT_FLOAT_EQ(ScrolledToShow(ScrollAt(0), D2D1::RectF(4, 54, 104, 254)).offsetDips, 50.0f);
}

TEST(Menu, TheThumbShowsHowMuchOfTheRowsIsInView) {
    const D2D1_RECT_F top = ThumbOrFail(ScrollAt(0));
    EXPECT_FLOAT_EQ(top.top, 4.0f);
    EXPECT_FLOAT_EQ(top.bottom, 29.0f);
    EXPECT_FLOAT_EQ(top.left, kTrack.left);
    EXPECT_FLOAT_EQ(top.right, kTrack.right);
    EXPECT_FLOAT_EQ(ThumbOrFail(ScrollAt(300)).bottom, 104.0f);
    const MenuScroll fits{.viewport = D2D1::RectF(4, 4, 104, 104), .contentDips = 100.0f, .offsetDips = 0.0f};
    EXPECT_EQ(ThumbRect(fits, kTrack), std::nullopt);
}

TEST(Menu, AThumbTooShortToGrabKeepsAMinimumLength) {
    const MenuScroll  many{.viewport = D2D1::RectF(4, 4, 104, 104), .contentDips = 10000.0f, .offsetDips = 0.0f};
    const D2D1_RECT_F thumb = ThumbOrFail(many);
    EXPECT_FLOAT_EQ(thumb.bottom - thumb.top, 16.0f);
}

TEST(Menu, DraggingTheThumbScrollsInProportion) {
    // The 25 DIP thumb travels 75 DIPs for 300 DIPs of scrolling.
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(0), kTrack, 4.0f + 37.5f).offsetDips, 150.0f);
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(0), kTrack, 200.0f).offsetDips, 300.0f);
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(150), kTrack, -50.0f).offsetDips, 0.0f);
}
