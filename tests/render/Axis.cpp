#include "render/Axis.hpp"

#include <gtest/gtest.h>

TEST(Contains, TakesTheLeftAndTopEdgesAndLeavesTheRightAndBottom) {
    const D2D1_RECT_F rect = D2D1::RectF(10.0f, 20.0f, 30.0f, 40.0f);
    EXPECT_TRUE(Contains(rect, D2D1::Point2F(10.0f, 20.0f)));
    EXPECT_TRUE(Contains(rect, D2D1::Point2F(29.5f, 39.5f)));
    EXPECT_FALSE(Contains(rect, D2D1::Point2F(30.0f, 25.0f)));
    EXPECT_FALSE(Contains(rect, D2D1::Point2F(15.0f, 40.0f)));
    EXPECT_FALSE(Contains(rect, D2D1::Point2F(9.5f, 25.0f)));
}
