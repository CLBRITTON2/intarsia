#include "modules/Workspaces.hpp"

#include "../wm/FakeWindowManager.hpp"
#include "TestStyle.hpp"
#include "TextOnlyRender.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

    const std::vector<Workspace> kWorkspaces{
        {
            .label = L"1",
            .name = "1",
            .focused = false,
            .occupied = true,
            .monitor = 10,
        },
        {
            .label = L"web",
            .name = "5",
            .focused = true,
            .occupied = true,
            .monitor = 10,
        },
        {
            .label = L"3",
            .name = "3",
            .focused = false,
            .occupied = false,
            .monitor = 10,
        },
        {
            .label = L"9",
            .name = "9",
            .focused = false,
            .occupied = true,
            .monitor = 20,
        },
    };

} // namespace

TEST(Workspaces, LayoutReportsAChangeOnlyWhenThisMonitorsWorkspacesChange) {
    FakeWindowManager windowManager;
    const TextOnlyRender render;
    Workspaces module(windowManager, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    windowManager.workspaces = kWorkspaces;
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_FALSE(module.Layout(render.Context()));
    windowManager.workspaces[3].focused = true;
    EXPECT_FALSE(module.Layout(render.Context()));
    windowManager.workspaces[2].occupied = true;
    EXPECT_TRUE(module.Layout(render.Context()));
}

TEST(Workspaces, HoverReportsAChangeOnlyWhenTheCursorMovesToAnotherItem) {
    FakeWindowManager windowManager;
    const TextOnlyRender render;
    Workspaces module(windowManager, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    windowManager.workspaces = {kWorkspaces[0], kWorkspaces[1]};
    module.Layout(render.Context());
    const float middle = module.Size().height / 2.0f;

    EXPECT_TRUE(module.Hover(D2D1::Point2F(6.0f, middle)));
    EXPECT_FALSE(module.Hover(D2D1::Point2F(7.0f, middle)));
    EXPECT_TRUE(module.Hover(D2D1::Point2F(module.Size().width - 6.0f, middle)));
    // Into the margin between items, then off the module.
    EXPECT_TRUE(module.Hover(D2D1::Point2F(module.Size().width - 2.0f, middle)));
    EXPECT_FALSE(module.Hover(std::nullopt));
}

TEST(Workspaces, ClickFocusesTheWorkspaceUnderItByNameAndNothingInTheGaps) {
    FakeWindowManager windowManager;
    const TextOnlyRender render;
    Workspaces module(windowManager, reinterpret_cast<HMONITOR>(10), TestModuleStyle());
    windowManager.workspaces = {kWorkspaces[0], kWorkspaces[1]};
    module.Layout(render.Context());
    const float height = module.Size().height;

    // Each item has 5 of margin on either side, which the module's width includes.
    module.Click(D2D1::Point2F(module.Size().width - 6.0f, height / 2.0f));
    EXPECT_EQ(windowManager.focusedWorkspace, "5");
    windowManager.focusedWorkspace.clear();
    module.Click(D2D1::Point2F(6.0f, height / 2.0f));
    EXPECT_EQ(windowManager.focusedWorkspace, "1");
    windowManager.focusedWorkspace.clear();
    module.Click(D2D1::Point2F(2.0f, height / 2.0f));
    module.Click(D2D1::Point2F(module.Size().width - 2.0f, height / 2.0f));
    EXPECT_TRUE(windowManager.focusedWorkspace.empty());
}
