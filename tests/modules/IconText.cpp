#include "modules/IconText.hpp"

#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

TEST(IconText, AnIconSittingLowInItsLineMovesUpAndOneSittingHighMovesDown) {
    const TextOnlyRender render;
    EXPECT_LT(LayOutIconText(render.Context(), L".", L"").iconInkShiftDips, -1.0f);
    EXPECT_GT(LayOutIconText(render.Context(), L"˙", L"").iconInkShiftDips, 1.0f);
}

TEST(IconText, AnIconWithNoInkStaysOnItsLine) {
    const TextOnlyRender render;
    EXPECT_FLOAT_EQ(LayOutIconText(render.Context(), L" ", L"").iconInkShiftDips, 0.0f);
}
