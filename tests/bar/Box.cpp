#include "bar/Box.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

    constexpr PixelGrid kFractionalScale{.pixelsPerDip = 1.25f, .offsetX = 0.0f, .offsetY = 0.0f};

    BorderSide          Side(float widthDips) {
        return BorderSide{.widthDips = widthDips, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)};
    }

    BoxStyle Lined(const Border& border, const std::vector<InsetShadow>& shadows) {
        return BoxStyle{.background = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), .border = border, .radiusDips = 0.0f, .padding = {}, .shadows = shadows};
    }

} // namespace

TEST(Box, SnapRectMovesEachEdgeToTheNearestDevicePixel) {
    const D2D1_RECT_F snapped = SnapRect(kFractionalScale, D2D1::RectF(10.3f, 0.5f, 20.0f, 30.9f));
    EXPECT_FLOAT_EQ(snapped.left, 10.4f);
    EXPECT_FLOAT_EQ(snapped.top, 0.8f);
    EXPECT_FLOAT_EQ(snapped.right, 20.0f);
    EXPECT_FLOAT_EQ(snapped.bottom, 31.2f);
}

TEST(Box, SnapRectCountsTheTransformsTranslation) {
    const PixelGrid   scrolled{.pixelsPerDip = 1.25f, .offsetX = 0.5f, .offsetY = 0.0f};
    const D2D1_RECT_F snapped = SnapRect(scrolled, D2D1::RectF(10.3f, 0.0f, 20.0f, 10.0f));
    // 10.3 DIPs lands on device pixel 13.5 once moved by the translation, which rounds to 14.
    EXPECT_FLOAT_EQ(snapped.left, 10.7f);
}

TEST(Box, SnapLinesRoundsBorderWidthsDownToWholePixelsButKeepsAtLeastOne) {
    const BoxStyle snapped = SnapLines(kFractionalScale, Lined(Border{.top = Side(1.0f), .right = Side(2.0f), .bottom = Side(0.5f), .left = Side(0.0f)}, {}));
    EXPECT_FLOAT_EQ(snapped.border.top.widthDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.border.right.widthDips, 1.6f);
    EXPECT_FLOAT_EQ(snapped.border.bottom.widthDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.border.left.widthDips, 0.0f);
}

TEST(Box, SnapLinesKeepsAWidthOfWholePixels) {
    const PixelGrid doubled{.pixelsPerDip = 2.0f, .offsetX = 0.0f, .offsetY = 0.0f};
    const BoxStyle  snapped = SnapLines(doubled, Lined(Border{.top = Side(1.0f), .right = Side(1.0f), .bottom = Side(1.0f), .left = Side(1.0f)}, {}));
    EXPECT_FLOAT_EQ(snapped.border.top.widthDips, 1.0f);
}

TEST(Box, SnapLinesRoundsShadowOffsetsAndSpreadButNotBlur) {
    const InsetShadow shadow{.xDips = 0.0f, .yDips = 1.0f, .blurDips = 14.0f, .spreadDips = -6.0f, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f)};
    const BoxStyle    snapped = SnapLines(kFractionalScale, Lined(Border{.top = Side(0.0f), .right = Side(0.0f), .bottom = Side(0.0f), .left = Side(0.0f)}, {shadow}));
    ASSERT_EQ(snapped.shadows.size(), 1u);
    EXPECT_FLOAT_EQ(snapped.shadows[0].yDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.shadows[0].spreadDips, -6.4f);
    EXPECT_FLOAT_EQ(snapped.shadows[0].blurDips, 14.0f);
}
