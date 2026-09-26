#include "modules/DirectionToggle.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

namespace {

    const TilingDirectionConfig kConfig{.horizontal = L"H", .vertical = L"V"};

} // namespace

TEST(DirectionToggle, ShowsTheConfiguredTextForTheDirection) {
    EXPECT_EQ(DirectionText(kConfig, TilingDirection::Horizontal), L"H");
    EXPECT_EQ(DirectionText(kConfig, TilingDirection::Vertical), L"V");
    EXPECT_EQ(DirectionText(kConfig, std::nullopt), L"");
}

TEST(DirectionToggle, LayoutReportsAChangeOnlyWhenTheDirectionChanges) {
    FakeWindowManager    windowManager;
    const TextOnlyRender render;
    DirectionToggle      module(kConfig, windowManager);

    windowManager.direction = TilingDirection::Horizontal;
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FALSE(module.Layout(render.Context()));
    windowManager.direction = TilingDirection::Vertical;
    EXPECT_TRUE(module.Layout(render.Context()));
}

TEST(DirectionToggle, ClickTogglesTheDirection) {
    FakeWindowManager windowManager;
    DirectionToggle   module(kConfig, windowManager);
    module.Click(D2D1::Point2F(0.0f, 0.0f));
    EXPECT_EQ(windowManager.directionToggles, 1);
}
