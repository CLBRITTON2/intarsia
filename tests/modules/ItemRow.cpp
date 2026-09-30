#include "modules/ItemRow.hpp"

#include "TestStyle.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(ItemRow, ItemsSitSideBySideWithTheirMarginsAndCenteredAcrossTheTallest) {
    const ItemStyle flat = TestItemStyle(0, 0);
    ItemStyle       tall = TestItemStyle(0, 0);
    tall.margin.yDips    = 3.0f;
    const ItemRow row    = ArrangeItems({{.width = 10.0f, .height = 10.0f}, {.width = 20.0f, .height = 10.0f}}, {&flat, &tall});

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

TEST(ItemRow, ItemAtFindsTheBoxAndNothingInItsMargins) {
    const ItemStyle style = TestItemStyle(0, 0);
    const ItemRow   row   = ArrangeItems({{.width = 10.0f, .height = 10.0f}, {.width = 10.0f, .height = 10.0f}}, {&style, &style});
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(2.0f, 5.0f)).has_value());
    EXPECT_EQ(ItemAt(row, D2D1::Point2F(5.0f, 5.0f)), 0u);
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(25.0f, 5.0f)).has_value());
    EXPECT_EQ(ItemAt(row, D2D1::Point2F(34.0f, 5.0f)), 1u);
    EXPECT_FALSE(ItemAt(row, D2D1::Point2F(34.0f, 20.0f)).has_value());
}
