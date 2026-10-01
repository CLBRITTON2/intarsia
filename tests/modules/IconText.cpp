#include "modules/IconText.hpp"

#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

TEST(IconText, AnIconSittingLowInItsLineMovesUpAndOneSittingHighMovesDown) {
    const TextOnlyRender render;
    EXPECT_LT(LayOutIconText(render.Context(), L".", L"").iconInkShiftDips, -1.0f);
    EXPECT_GT(LayOutIconText(render.Context(), L"˙", L"").iconInkShiftDips, 1.0f);
}

TEST(IconText, AnIconWithNoInkStaysOnItsLine) {
    const TextOnlyRender render;
    EXPECT_FLOAT_EQ(LayOutIconText(render.Context(), L" ", L"").iconInkShiftDips, 0.0f);
}

TEST(IconText, AlongAHorizontalBarTheTextFollowsTheIconUncut) {
    const TextOnlyRender render;
    const IconText       laidOut = LayOutIconText(render.Context(), L"A", L"a line far wider than the bar is thick");
    EXPECT_FLOAT_EQ(laidOut.size.width, laidOut.iconSize.width + 4.0f + laidOut.textSize.width);
    EXPECT_GT(laidOut.textSize.width, 100.0f);
}

TEST(IconText, DownAVerticalBarTheTextGoesUnderTheIcon) {
    const TextOnlyRender render;
    const IconText       laidOut = LayOutIconText(render.Along(Axis::Vertical, 100.0f), L"A", L"Mon");
    EXPECT_FLOAT_EQ(laidOut.size.height, laidOut.iconSize.height + 4.0f + laidOut.textSize.height);
    EXPECT_FLOAT_EQ(laidOut.size.width, std::max(laidOut.iconSize.width, laidOut.textSize.width));
}

TEST(IconText, DownAVerticalBarTextWiderThanTheBarIsCut) {
    const TextOnlyRender render;
    const std::wstring   line  = L"a line far wider than the bar is thick";
    const float          whole = LayOutIconText(render.Context(), L"", line).textSize.width;
    const float          cut   = LayOutIconText(render.Along(Axis::Vertical, 40.0f), L"", line).textSize.width;
    EXPECT_LE(cut, 40.0f);
    EXPECT_GT(cut, 0.0f);
    EXPECT_LT(cut, whole);
}

TEST(IconText, AnItemsTextIsCutInsideItsInset) {
    const TextOnlyRender render;
    EXPECT_LE(LayOutWeightedText(render.Along(Axis::Vertical, 40.0f), L"a long workspace name", DWRITE_FONT_WEIGHT_NORMAL, 10.0f).textSize.width, 20.0f);
}

TEST(IconText, DownAVerticalBarALineBreakStacksTheLines) {
    const TextOnlyRender render;
    const float          one = LayOutIconText(render.Along(Axis::Vertical, 40.0f), L"", L"12").textSize.height;
    const IconText       two = LayOutIconText(render.Along(Axis::Vertical, 40.0f), L"", L"12\n34");
    EXPECT_FLOAT_EQ(two.textSize.height, 2.0f * one);
    // The lines are centered in the room, so they start in from its left.
    EXPECT_GT(two.textLeftDips, 0.0f);
}
