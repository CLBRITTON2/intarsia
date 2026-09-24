#include "modules/Workspaces.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

    const std::vector<Workspace> kWorkspaces{
        {.label = L"1", .name = "1", .focused = false, .monitor = 10},
        {.label = L"web", .name = "5", .focused = true, .monitor = 10},
        {.label = L"9", .name = "9", .focused = false, .monitor = 20},
    };

} // namespace

TEST(Workspaces, JoinsOnlyThisMonitorsLabelsWithTwoSpaces) {
    const WorkspaceLine line = JoinWorkspaces(kWorkspaces, 10);
    EXPECT_EQ(line.text, L"1  web");
    ASSERT_EQ(line.runs.size(), 2u);
    EXPECT_EQ(line.runs[1].start, 3u);
    EXPECT_EQ(line.runs[1].length, 3u);
    EXPECT_TRUE(line.runs[1].focused);
    EXPECT_EQ(line.runs[1].name, "5");
    EXPECT_FALSE(line.runs[0].focused);
}

TEST(Workspaces, NoWorkspacesOnTheMonitorGivesAnEmptyLine) {
    const WorkspaceLine line = JoinWorkspaces(kWorkspaces, 30);
    EXPECT_TRUE(line.text.empty());
    EXPECT_TRUE(line.runs.empty());
}

TEST(Workspaces, WorkspaceAtMapsALabelToItsNameNotItsDisplayName) {
    const WorkspaceLine line = JoinWorkspaces(kWorkspaces, 10);
    EXPECT_EQ(WorkspaceAt(line, 0), "1");
    EXPECT_EQ(WorkspaceAt(line, 3), "5");
    EXPECT_EQ(WorkspaceAt(line, 5), "5");
}

TEST(Workspaces, WorkspaceAtIsEmptyBetweenAndAfterLabels) {
    const WorkspaceLine line = JoinWorkspaces(kWorkspaces, 10);
    EXPECT_FALSE(WorkspaceAt(line, 1).has_value());
    EXPECT_FALSE(WorkspaceAt(line, 2).has_value());
    EXPECT_FALSE(WorkspaceAt(line, 6).has_value());
}
