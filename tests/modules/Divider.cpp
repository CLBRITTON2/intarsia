#include "modules/Divider.hpp"

#include "TestStyle.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

TEST(Divider, StandsAcrossAHorizontalBarAndLiesFlatAcrossAVerticalOne) {
    const TextOnlyRender render;
    Divider              divider(TestModuleStyle());
    EXPECT_TRUE(divider.Layout(render.Along(Axis::Horizontal, 24.0f)));
    EXPECT_FALSE(divider.Layout(render.Along(Axis::Horizontal, 24.0f)));
    // 1 thick along the bar, half the box across it.
    EXPECT_FLOAT_EQ(divider.Size().width, 1.0f);
    EXPECT_FLOAT_EQ(divider.Size().height, 12.0f);

    EXPECT_TRUE(divider.Layout(render.Along(Axis::Vertical, 40.0f)));
    EXPECT_FLOAT_EQ(divider.Size().width, 20.0f);
    EXPECT_FLOAT_EQ(divider.Size().height, 1.0f);
}
