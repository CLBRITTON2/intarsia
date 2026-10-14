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

TEST(Offset, MovesEveryEdgeByThePoint) {
    const D2D1_RECT_F moved = Offset(D2D1::RectF(1.0f, 2.0f, 3.0f, 4.0f), D2D1::Point2F(10.0f, -20.0f));
    EXPECT_FLOAT_EQ(moved.left, 11.0f);
    EXPECT_FLOAT_EQ(moved.top, -18.0f);
    EXPECT_FLOAT_EQ(moved.right, 13.0f);
    EXPECT_FLOAT_EQ(moved.bottom, -16.0f);
}

TEST(Intersection, KeepsThePartBothRectsCover) {
    const D2D1_RECT_F shared = Intersection(D2D1::RectF(0.0f, 0.0f, 20.0f, 10.0f), D2D1::RectF(15.0f, 5.0f, 40.0f, 30.0f));
    EXPECT_FLOAT_EQ(shared.left, 15.0f);
    EXPECT_FLOAT_EQ(shared.top, 5.0f);
    EXPECT_FLOAT_EQ(shared.right, 20.0f);
    EXPECT_FLOAT_EQ(shared.bottom, 10.0f);
}

TEST(Intersection, OfRectsThatDoNotMeetIsEmpty) {
    const D2D1_RECT_F apart = Intersection(D2D1::RectF(0.0f, 0.0f, 10.0f, 10.0f), D2D1::RectF(20.0f, 0.0f, 30.0f, 10.0f));
    EXPECT_LT(apart.right, apart.left);
}
