#include "bar/Layout.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

    EndInsets Both(float inset) {
        return EndInsets{.start = inset, .end = inset};
    }

} // namespace

TEST(Layout, ASectionWhoseModulesAllShowNothingHasNoLength) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {0.0f, 0.0f},
            .center = {},
            .right = {30.0f, 0.0f},
        },
        1000.0f, 5.0f, Both(4.0f));
    EXPECT_EQ(layout.left.length, 0.0f);
    EXPECT_EQ(layout.center.length, 0.0f);
    EXPECT_TRUE(layout.center.offsets.empty());
    // An empty module adds nothing, so the section ends one padding after the module before it.
    EXPECT_EQ(layout.right.length, 38.0f);
    EXPECT_EQ(layout.right.start, 1000.0f - 5.0f - 38.0f);
}

TEST(Layout, LeftStartsOneMarginInAndPadsItsModules) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {100.0f, 50.0f},
            .center = {},
            .right = {},
        },
        1000.0f, 5.0f, Both(4.0f));
    EXPECT_EQ(layout.left.start, 5.0f);
    EXPECT_EQ(layout.left.length, 158.0f);
    EXPECT_EQ(layout.left.offsets, (std::vector<float>{9.0f, 109.0f}));
}

TEST(Layout, RightEndsOneMarginBeforeTheEnd) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {},
            .center = {},
            .right = {100.0f, 50.0f},
        },
        1000.0f, 5.0f, Both(4.0f));
    EXPECT_EQ(layout.right.start + layout.right.length, 995.0f);
    EXPECT_EQ(layout.right.offsets, (std::vector<float>{841.0f, 941.0f}));
}

TEST(Layout, ASectionsModulesStartPastItsStartInsetAndEndBeforeItsEndInset) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {100.0f},
            .center = {},
            .right = {100.0f},
        },
        1000.0f, 5.0f, EndInsets{.start = 4.0f, .end = 1.0f});
    EXPECT_EQ(layout.left.length, 105.0f);
    EXPECT_EQ(layout.left.offsets, (std::vector<float>{9.0f}));
    EXPECT_EQ(layout.right.offsets, (std::vector<float>{894.0f}));
}

TEST(Layout, CenterIsCenteredOnTheWholeBar) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {300.0f},
            .center = {100.0f, 100.0f},
            .right = {},
        },
        1000.0f, 5.0f, Both(10.0f));
    EXPECT_EQ(layout.center.start, 390.0f);
    EXPECT_EQ(layout.center.offsets, (std::vector<float>{400.0f, 500.0f}));
}

TEST(Layout, AFoldedGroupIsItsToggleInsideItsPadding) {
    const GroupLayout group = ArrangeGroup(20.0f, {30.0f, 50.0f}, 0.0f, Both(4.0f));
    EXPECT_EQ(group.length, 28.0f);
    EXPECT_EQ(group.toggleLength, 28.0f);
    // The members stay where the unfolded group puts them, past the box's end.
    EXPECT_EQ(group.offsets, (std::vector<float>{28.0f, 58.0f}));
    EXPECT_EQ(group.shownLength, 0.0f);
}

TEST(Layout, AGroupsToggleTakesBothItsInsets) {
    const GroupLayout group = ArrangeGroup(20.0f, {30.0f}, 1.0f, EndInsets{.start = 4.0f, .end = 1.0f});
    EXPECT_EQ(group.toggleLength, 25.0f);
    EXPECT_EQ(group.offsets, (std::vector<float>{25.0f}));
}

TEST(Layout, AnUnfoldedGroupPlacesItsMembersBoxToBoxAfterItsPaddedToggle) {
    const GroupLayout group = ArrangeGroup(20.0f, {30.0f, 0.0f, 50.0f}, 1.0f, Both(4.0f));
    EXPECT_EQ(group.length, 108.0f);
    EXPECT_EQ(group.toggleLength, 28.0f);
    EXPECT_EQ(group.offsets, (std::vector<float>{28.0f, 58.0f, 58.0f}));
    EXPECT_EQ(group.shownLength, 80.0f);
}

TEST(Layout, AGroupMidWayTakesItsShareOfItsMembers) {
    const GroupLayout group = ArrangeGroup(20.0f, {30.0f, 50.0f}, 0.25f, Both(4.0f));
    EXPECT_EQ(group.length, 48.0f);
    EXPECT_EQ(group.shownLength, 20.0f);
}

TEST(Layout, CenterWiderThanTheBarOverflowsBothEndsEqually) {
    const BarLayout layout = ArrangeBar(
        {
            .left = {},
            .center = {200.0f},
            .right = {},
        },
        100.0f, 0.0f, Both(0.0f));
    EXPECT_EQ(layout.center.offsets, (std::vector<float>{-50.0f}));
}

TEST(Layout, MainAndCrossInsetsFollowTheBarsAxis) {
    BoxStyle style{};
    style.padding = Spacing{.yDips = 2.0f, .xDips = 6.0f};
    EXPECT_EQ(MainInsets(style, Axis::Horizontal).start, 6.0f);
    EXPECT_EQ(MainInsets(style, Axis::Vertical).start, 2.0f);
    EXPECT_EQ(CrossInsets(style, Axis::Horizontal).end, 2.0f);
    EXPECT_EQ(CrossInsets(style, Axis::Vertical).end, 6.0f);
}

TEST(Layout, ContentStartsPastTheInsetsAlongTheBarAndCenteredAcrossIt) {
    const Insets insets{
        .left = 4.0f,
        .top = 2.0f,
        .right = 4.0f,
        .bottom = 2.0f,
    };
    const D2D1_RECT_F box = D2D1::RectF(10.0f, 0.0f, 60.0f, 30.0f);
    const D2D1_POINT_2F horizontal = ContentOrigin(Axis::Horizontal, box, insets, D2D1::SizeF(20.0f, 10.0f));
    EXPECT_EQ(horizontal.x, 14.0f);
    EXPECT_EQ(horizontal.y, 10.0f);
    const D2D1_POINT_2F vertical = ContentOrigin(Axis::Vertical, box, insets, D2D1::SizeF(20.0f, 10.0f));
    EXPECT_EQ(vertical.x, 25.0f);
    EXPECT_EQ(vertical.y, 2.0f);
}

TEST(Layout, ABoxSpansTheBarInsideTheSectionsMarginAndInsets) {
    SectionStyle section{};
    section.margin = Spacing{.yDips = 3.0f, .xDips = 5.0f};
    section.box.padding = Spacing{.yDips = 1.0f, .xDips = 0.0f};
    const BarGeometry bar{
        .axis = Axis::Horizontal,
        .section = section,
        .size = D2D1::SizeF(1000.0f, 30.0f),
    };
    EXPECT_EQ(BoxCrossLength(bar), 22.0f);
    const D2D1_RECT_F bounds = SectionBounds(bar,
                                             SectionLayout{
                                                 .start = 5.0f,
                                                 .length = 100.0f,
                                                 .offsets = {},
                                             });
    EXPECT_EQ(bounds.left, 5.0f);
    EXPECT_EQ(bounds.top, 3.0f);
    EXPECT_EQ(bounds.right, 105.0f);
    EXPECT_EQ(bounds.bottom, 27.0f);
}

TEST(Layout, AGroupsMembersTakeTheCursorBeforeItAndOnlyWhereTheFoldUncoversThem) {
    const HitBox toggle{
        .module = nullptr,
        .bounds = D2D1::RectF(0.0f, 0.0f, 50.0f, 20.0f),
        .contentOrigin = D2D1::Point2F(0.0f, 0.0f),
    };
    const HitBox halfShown{
        .module = nullptr,
        .bounds = D2D1::RectF(20.0f, 0.0f, 40.0f, 20.0f),
        .contentOrigin = D2D1::Point2F(22.0f, 0.0f),
    };
    const HitBox folded{
        .module = nullptr,
        .bounds = D2D1::RectF(40.0f, 0.0f, 60.0f, 20.0f),
        .contentOrigin = D2D1::Point2F(42.0f, 0.0f),
    };
    Sections<PlacedEntry> entries{};
    entries.left.push_back(PlacedEntry{
        .entry = nullptr,
        .box = toggle,
        .toggleBounds = D2D1::RectF(0.0f, 0.0f, 20.0f, 20.0f),
        .members = {halfShown, folded},
        .shownBounds = D2D1::RectF(20.0f, 0.0f, 30.0f, 20.0f),
    });
    const std::vector<HitBox> boxes = HitBoxes(entries);
    ASSERT_EQ(boxes.size(), 2u);
    EXPECT_EQ(boxes[0].bounds.left, 20.0f);
    EXPECT_EQ(boxes[0].bounds.right, 30.0f);
    EXPECT_EQ(boxes[0].contentOrigin.x, 22.0f);
    EXPECT_EQ(boxes[1].bounds.right, 50.0f);
}
