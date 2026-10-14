#include "bar/Menu.hpp"
#include "bar/Tooltip.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace {

    MenuRow RowAt(float top, std::optional<size_t> entry) {
        return MenuRow{
            .text       = nullptr,
            .box        = D2D1::RectF(4, top, 104, top + 20),
            .textOrigin = D2D1::Point2F(0, 0),
            .style      = nullptr,
            .entry      = entry,
        };
    }

    // A 100 DIP viewport over contentDips of rows, beside a track as tall as the viewport.
    MenuScroll ScrollOver(float contentDips, float offsetDips) {
        return MenuScroll{
            .viewport    = D2D1::RectF(4, 4, 104, 104),
            .contentDips = contentDips,
            .offsetDips  = offsetDips,
        };
    }

    MenuScroll ScrollAt(float offsetDips) {
        return ScrollOver(400.0f, offsetDips);
    }

    const D2D1_RECT_F    kTrack = D2D1::RectF(106, 4, 110, 104);

    constexpr RECT       kMonitor{0, 0, 1920, 1080};

    const ScrollbarStyle kScrollbar{
        .widthDips  = 4,
        .background = {},
        .radiusDips = 0,
        .margin     = Spacing{.yDips = 2, .xDips = 1},
    };

    // A 4 DIP padded box of rows 2 DIPs apart vertically and 3 DIPs in from its sides, with no scroll bar.
    MenuStyle PaddedMenu() {
        MenuStyle style{};
        style.box.padding  = Spacing{.yDips = 4, .xDips = 4};
        style.item.margin  = Spacing{.yDips = 2, .xDips = 3};
        style.item.padding = Spacing{.yDips = 1, .xDips = 5};
        return style;
    }

    // A row as LayOutRow measures it: sized to its text, at the origin.
    MenuRow MeasuredRow(float width, const ItemStyle& style, std::optional<size_t> entry) {
        return MenuRow{
            .text       = nullptr,
            .box        = D2D1::RectF(0, 0, width, 20),
            .textOrigin = D2D1::Point2F(0, 0),
            .style      = &style,
            .entry      = entry,
        };
    }

    void ExpectRect(const D2D1_RECT_F& actual, const D2D1_RECT_F& expected) {
        EXPECT_FLOAT_EQ(actual.left, expected.left);
        EXPECT_FLOAT_EQ(actual.top, expected.top);
        EXPECT_FLOAT_EQ(actual.right, expected.right);
        EXPECT_FLOAT_EQ(actual.bottom, expected.bottom);
    }

    D2D1_RECT_F ThumbOrFail(const MenuScroll& scroll) {
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
    constexpr SIZE kSize{.cx = 100, .cy = 30};
    const RECT     below = TooltipRect(RowTooltipAnchor(RECT{1600, 300, 1800, 320}, kMonitor, 96), kSize);
    EXPECT_EQ(below.left, 1650);
    EXPECT_EQ(below.top, 320);
    const RECT above = TooltipRect(RowTooltipAnchor(RECT{1600, 900, 1800, 920}, kMonitor, 96), kSize);
    EXPECT_EQ(above.top, 870);
    // A tooltip wider than its row near the monitor's edge stays on the monitor.
    const RECT wide = TooltipRect(RowTooltipAnchor(RECT{1700, 300, 1900, 320}, kMonitor, 96), SIZE{.cx = 400, .cy = 30});
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
    const MenuScroll fits = ScrollOver(80.0f, 0.0f);
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
    const MenuScroll fits = ScrollOver(100.0f, 0.0f);
    EXPECT_EQ(ThumbRect(fits, kTrack), std::nullopt);
}

TEST(Menu, AThumbTooShortToGrabKeepsAMinimumLength) {
    const MenuScroll  many  = ScrollOver(10000.0f, 0.0f);
    const D2D1_RECT_F thumb = ThumbOrFail(many);
    EXPECT_FLOAT_EQ(thumb.bottom - thumb.top, 16.0f);
}

TEST(Menu, DraggingTheThumbScrollsInProportion) {
    // The 25 DIP thumb travels 75 DIPs for 300 DIPs of scrolling.
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(0), kTrack, 4.0f + 37.5f).offsetDips, 150.0f);
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(0), kTrack, 200.0f).offsetDips, 300.0f);
    EXPECT_FLOAT_EQ(ScrolledToThumb(ScrollAt(150), kTrack, -50.0f).offsetDips, 0.0f);
}

TEST(Menu, RowsStackInsideTheBoxAndStretchToTheWidest) {
    const MenuStyle  style  = PaddedMenu();
    const MenuLayout layout = LayOutMenu({MeasuredRow(50, style.item, 0), MeasuredRow(80, style.item, 1)}, style, kMonitor, 96);
    ASSERT_EQ(layout.rows.size(), 2u);
    ExpectRect(layout.rows[0].box, D2D1::RectF(7, 6, 87, 26));
    ExpectRect(layout.rows[1].box, D2D1::RectF(7, 30, 87, 50));
    EXPECT_FLOAT_EQ(layout.rows[1].textOrigin.x, 12.0f);
    EXPECT_FLOAT_EQ(layout.rows[1].textOrigin.y, 31.0f);
    EXPECT_EQ(layout.size.cx, 94);
    EXPECT_EQ(layout.size.cy, 56);
    ExpectRect(layout.scroll.viewport, D2D1::RectF(4, 4, 90, 52));
    EXPECT_FLOAT_EQ(layout.scroll.contentDips, 48.0f);
    EXPECT_EQ(ThumbRect(layout.scroll, layout.track), std::nullopt);
}

TEST(Menu, RowsPastMaxHeightScrollBesideTheScrollBar) {
    MenuStyle style         = PaddedMenu();
    style.maxHeightDips     = 40.0f;
    style.scrollbar         = kScrollbar;
    const MenuLayout layout = LayOutMenu({MeasuredRow(50, style.item, 0), MeasuredRow(80, style.item, 1)}, style, kMonitor, 96);
    EXPECT_EQ(layout.size.cx, 100);
    EXPECT_EQ(layout.size.cy, 40);
    ExpectRect(layout.scroll.viewport, D2D1::RectF(4, 4, 90, 36));
    ExpectRect(layout.gutter, D2D1::RectF(90, 4, 96, 36));
    ExpectRect(layout.track, D2D1::RectF(91, 6, 95, 34));
    EXPECT_NE(ThumbRect(layout.scroll, layout.track), std::nullopt);
}

TEST(Menu, AMenuNeverOutgrowsItsMonitor) {
    const MenuStyle  style  = PaddedMenu();
    const MenuLayout layout = LayOutMenu({MeasuredRow(50, style.item, 0), MeasuredRow(80, style.item, 1)}, style, RECT{0, 0, 60, 30}, 96);
    EXPECT_EQ(layout.size.cx, 60);
    EXPECT_EQ(layout.size.cy, 30);
}

TEST(Menu, RowTextIsCutAtTheNarrowerOfMaxWidthAndTheMonitor) {
    MenuStyle style    = PaddedMenu();
    style.maxWidthDips = 300.0f;
    style.scrollbar    = kScrollbar;
    // Less the gutter, the box's padding and the row's margin and padding, on both sides.
    EXPECT_FLOAT_EQ(MenuTextWidth(style, style.item, kMonitor, 96), 270.0f);
    EXPECT_FLOAT_EQ(MenuTextWidth(style, style.item, RECT{0, 0, 200, 1080}, 96), 170.0f);
    // A 200 pixel monitor at 150% is 133 DIPs wide.
    EXPECT_NEAR(MenuTextWidth(style, style.item, RECT{0, 0, 200, 1080}, 144), 103.333f, 0.001f);
}
