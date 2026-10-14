#include "bar/PixelGrid.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>

namespace {

    constexpr PixelGrid kFractionalScale{
        .pixelsPerDip = 1.25f,
        .offsetX      = 0.0f,
        .offsetY      = 0.0f,
    };

} // namespace

TEST(PixelGrid, SnapRectMovesEachEdgeToTheNearestDevicePixel) {
    const D2D1_RECT_F snapped = SnapRect(kFractionalScale, D2D1::RectF(10.3f, 0.5f, 20.0f, 30.9f));
    EXPECT_FLOAT_EQ(snapped.left, 10.4f);
    EXPECT_FLOAT_EQ(snapped.top, 0.8f);
    EXPECT_FLOAT_EQ(snapped.right, 20.0f);
    EXPECT_FLOAT_EQ(snapped.bottom, 31.2f);
}

TEST(PixelGrid, SnapRectCountsTheTransformsTranslation) {
    const PixelGrid scrolled{
        .pixelsPerDip = 1.25f,
        .offsetX      = 0.5f,
        .offsetY      = 0.0f,
    };
    const D2D1_RECT_F snapped = SnapRect(scrolled, D2D1::RectF(10.3f, 0.0f, 20.0f, 10.0f));
    // 10.3 DIPs lands on device pixel 13.5 once moved by the translation, which rounds to 14.
    EXPECT_FLOAT_EQ(snapped.left, 10.7f);
}
