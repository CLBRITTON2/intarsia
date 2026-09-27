#include "modules/BindingModes.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TestStyle.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

TEST(BindingModes, LayoutReportsAChangeOnlyWhenTheModesChange) {
    FakeWindowManager    windowManager;
    const TextOnlyRender render;
    BindingModes         module(windowManager, TestModuleStyle());

    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FALSE(module.Layout(render.Context()));
    windowManager.bindingModes = {L"resize"};
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_GT(module.Size().width, 0.0f);
    windowManager.bindingModes.clear();
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FLOAT_EQ(module.Size().width, 0.0f);
}

TEST(BindingModes, EachModeIsAPaddedItemWithItsMarginsBetween) {
    FakeWindowManager    windowManager;
    const TextOnlyRender render;
    BindingModes         module(windowManager, TestModuleStyle());
    windowManager.bindingModes = {L"resize"};
    module.Layout(render.Context());
    const float one = module.Size().width;

    windowManager.bindingModes = {L"resize", L"resize"};
    module.Layout(render.Context());
    // Each item carries its margins, so two of the same take twice the width.
    EXPECT_FLOAT_EQ(module.Size().width, 2.0f * one);
}
