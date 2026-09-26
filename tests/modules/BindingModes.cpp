#include "modules/BindingModes.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

TEST(BindingModes, JoinsModeNamesWithTwoSpaces) {
    EXPECT_EQ(JoinModes({}), L"");
    EXPECT_EQ(JoinModes({L"resize"}), L"resize");
    EXPECT_EQ(JoinModes({L"resize", L"pause"}), L"resize  pause");
}

TEST(BindingModes, LayoutReportsAChangeOnlyWhenTheModesChange) {
    FakeWindowManager    windowManager;
    const TextOnlyRender render;
    BindingModes         module(windowManager);

    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FALSE(module.Layout(render.Context()));
    windowManager.bindingModes = {L"resize"};
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_GT(module.Size().width, 0.0f);
    windowManager.bindingModes.clear();
    EXPECT_TRUE(module.Layout(render.Context()));
}
