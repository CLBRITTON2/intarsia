#include "modules/ItemRow.hpp"

#include "TestStyle.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

TEST(ItemRow, ItemsSitSideBySideWithTheirMarginsAndCenteredAcrossTheTallest) {
    const ItemStyle flat = TestItemStyle(0, 0);
    ItemStyle tall = TestItemStyle(0, 0);
    tall.margin.yDips = 3.0f;
    const ItemRow row = ArrangeItems({{.width = 10.0f, .height = 10.0f}, {.width = 20.0f, .height = 10.0f}}, {&flat, &tall}, Axis::Horizontal);

    // 10 and 20 wide padded by 4 each side, 5 of margin around each. The tall item's margin sets the height: 10 + 4 + 6.
    EXPECT_FLOAT_EQ(row.size.width, 66.0f);
    EXPECT_FLOAT_EQ(row.size.height, 20.0f);
    ASSERT_EQ(row.boxes.size(), 2u);
    EXPECT_FLOAT_EQ(row.boxes[0].left, 5.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].right, 23.0f);
    EXPECT_FLOAT_EQ(row.boxes[1].left, 33.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].top, 3.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].bottom, 17.0f);
}

TEST(ItemRow, DownAVerticalBarItemsStackWithTheirMarginsAndCenterAcrossTheWidest) {
    const ItemStyle flat = TestItemStyle(0, 0);
    ItemStyle wide = TestItemStyle(0, 0);
    wide.margin.yDips = 3.0f;
    const ItemRow row = ArrangeItems({{.width = 10.0f, .height = 10.0f}, {.width = 20.0f, .height = 10.0f}}, {&flat, &wide}, Axis::Vertical);

    // 10 tall padded by 2 above and below, the first with no margin there and the second with 3. The wide item sets
    // the width: 20 + 8 + 10.
    EXPECT_FLOAT_EQ(row.size.height, 34.0f);
    EXPECT_FLOAT_EQ(row.size.width, 38.0f);
    ASSERT_EQ(row.boxes.size(), 2u);
    EXPECT_FLOAT_EQ(row.boxes[0].top, 0.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].bottom, 14.0f);
    EXPECT_FLOAT_EQ(row.boxes[1].top, 17.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].left, 10.0f);
    EXPECT_FLOAT_EQ(row.boxes[0].right, 28.0f);
    EXPECT_FLOAT_EQ(row.boxes[1].left, 5.0f);
}

TEST(ItemRow, ItemAtFindsTheBoxAndNothingInItsMargins) {
    const ItemStyle style = TestItemStyle(0, 0);
    const ItemRow row = ArrangeItems({{.width = 10.0f, .height = 10.0f}, {.width = 10.0f, .height = 10.0f}}, {&style, &style}, Axis::Horizontal);
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(2.0f, 5.0f)).has_value());
    EXPECT_EQ(ItemAt(row, D2D1::Point2F(5.0f, 5.0f)), 0u);
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(25.0f, 5.0f)).has_value());
    EXPECT_EQ(ItemAt(row, D2D1::Point2F(34.0f, 5.0f)), 1u);
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(34.0f, 20.0f)).has_value());
}

TEST(ItemRow, ItemFadesFadeAnItemIntoItsHoverAndStartNewItemsAtTheirColors) {
    ItemStyle style = TestItemStyle(0x000000, 0x000000);
    style.hoverBackground = D2D1::ColorF(0xFFFFFF);
    style.backgroundTransition = {.duration = std::chrono::milliseconds(100), .timing = TimingFunction::Linear};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    HoveredItem hovered;
    ItemFades fades;
    fades.Follow({&style}, hovered, start);
    EXPECT_EQ(fades.Wait(start), std::nullopt);

    EXPECT_TRUE(hovered.Set(0));
    fades.Follow({&style, &style}, hovered, start);
    EXPECT_EQ(fades.Wait(start), 0U);
    EXPECT_NEAR(fades.Background(0, start + std::chrono::milliseconds(50)).r, 0.5f, 0.001f);
    // The text has no transition of its own, so it takes the hover's at once.
    EXPECT_FLOAT_EQ(fades.Text(0, start).r, 1.0f);
    EXPECT_FLOAT_EQ(fades.Background(1, start).r, 0.0f);
    EXPECT_EQ(fades.Wait(start + std::chrono::milliseconds(100)), std::nullopt);
}

TEST(ItemRow, ItemFadesFadeAnItemIntoANewStatesColors) {
    const ItemStyle rest = TestItemStyle(0x000000, 0x000000);
    ItemStyle focused = TestItemStyle(0xFFFFFF, 0xFFFFFF);
    focused.colorTransition = {.duration = std::chrono::milliseconds(100), .timing = TimingFunction::Linear};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const HoveredItem hovered;
    ItemFades fades;
    fades.Follow({&rest}, hovered, start);
    // The target's transition applies, as in CSS: the focused style's color fades and its background does not.
    fades.Follow({&focused}, hovered, start);
    EXPECT_NEAR(fades.Text(0, start + std::chrono::milliseconds(50)).r, 0.5f, 0.001f);
    EXPECT_FLOAT_EQ(fades.Background(0, start).r, 1.0f);
}

TEST(ItemRow, ItemFadesFadeANewStatesShadowsOverItsBackgroundTransition) {
    const ItemStyle rest = TestItemStyle(0x000000, 0x000000);
    ItemStyle focused = TestItemStyle(0xFFFFFF, 0xFFFFFF);
    focused.backgroundTransition = {.duration = std::chrono::milliseconds(100), .timing = TimingFunction::Linear};
    focused.shadows = {{
        .xDips = 0.0f,
        .yDips = 1.0f,
        .blurDips = 0.0f,
        .spreadDips = 0.0f,
        .color = D2D1::ColorF(0xFFFFFF),
    }};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const HoveredItem hovered;
    ItemFades fades;
    fades.Follow({&rest}, hovered, start);
    EXPECT_TRUE(fades.Shadows(0, start).empty());
    fades.Follow({&focused}, hovered, start);
    const std::vector<InsetShadow> halfway = fades.Shadows(0, start + std::chrono::milliseconds(50));
    ASSERT_EQ(halfway.size(), 1u);
    EXPECT_NEAR(halfway[0].color.a, 0.5f, 0.001f);
    EXPECT_EQ(fades.Wait(start + std::chrono::milliseconds(50)), 0U);
}
