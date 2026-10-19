#include "modules/Window.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>

TEST(Window, LayoutReportsAChangeOnlyWhenTheTitleChanges) {
    FakeWindowManager windowManager;
    const TextOnlyRender render;
    Window window(
        WindowConfig{
            .maxLength = 50,
            .tooltip = std::nullopt,
            .actions = kNoActions,
        },
        windowManager);

    windowManager.focused = FocusedWindow{.title = L"one"};
    EXPECT_TRUE(window.Layout(render.Context()));
    EXPECT_FALSE(window.Layout(render.Context()));
    windowManager.focused = FocusedWindow{.title = L"two"};
    EXPECT_TRUE(window.Layout(render.Context()));
    windowManager.focused = std::nullopt;
    EXPECT_TRUE(window.Layout(render.Context()));
    EXPECT_FALSE(window.Layout(render.Context()));
}

TEST(Window, LongTitleEndsInAnEllipsisAtTheLimit) {
    EXPECT_EQ(TruncateTitle(L"intarsia", 8), L"intarsia");
    const std::wstring title = TruncateTitle(L"intarsia - Visual Studio Code", 10);
    EXPECT_EQ(title, L"intarsia …");
    EXPECT_EQ(title.size(), 10u);
}

TEST(Window, TruncationNeverSplitsASurrogatePair) {
    const std::wstring title = L"ab\U000F1441cd";
    EXPECT_EQ(TruncateTitle(title, 4), L"ab…");
}
