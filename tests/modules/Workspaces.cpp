#include "modules/Workspaces.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(Workspaces, JoinsOnlyThisMonitorsLabelsWithTwoSpaces) {
    const std::vector<Workspace> workspaces{
        {.label = L"1", .focused = false, .monitor = 10},
        {.label = L"web", .focused = true, .monitor = 10},
        {.label = L"9", .focused = false, .monitor = 20},
    };
    const WorkspaceLine line = JoinWorkspaces(workspaces, 10);
    EXPECT_EQ(line.text, L"1  web");
    ASSERT_EQ(line.runs.size(), 2u);
    EXPECT_EQ(line.runs[1].start, 3u);
    EXPECT_EQ(line.runs[1].length, 3u);
    EXPECT_TRUE(line.runs[1].focused);
    EXPECT_FALSE(line.runs[0].focused);
}

TEST(Workspaces, NoWorkspacesOnTheMonitorGivesAnEmptyLine) {
    const WorkspaceLine line = JoinWorkspaces({{.label = L"1", .focused = true, .monitor = 20}}, 10);
    EXPECT_TRUE(line.text.empty());
    EXPECT_TRUE(line.runs.empty());
}
