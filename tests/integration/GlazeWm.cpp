#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>

using testing::ElementsAre;

namespace {

    // Every module that sends GlazeWM a command on a click, one per place along the bar. The test server shows
    // workspaces 1 and 2 on the primary monitor, 2 displayed and holding the test's windows.
    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "workspaces", "apps" },
    modules_center = { "clock" },
    modules_right = { "tiling" },
    layouts = {},
    modules = {
        workspaces = { type = "workspaces" },
        apps = { type = "windows" },
        clock = { type = "clock", format = "{time:%H:%M:%S}", icon = "", interval = 1,
                  on_click = { window_manager = "focus --next-workspace" } },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },
    },
}
)";

    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 13px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
.module { padding: 0 8px; }
.item { padding: 2px 6px; margin: 0 1px; }
.item.focused { color: #191724; background: #9ccfd8; }
)";

    // The ids the test server gives the windows it lists, in its order (tests/stress/GlazeWmServer.cpp).
    constexpr std::string_view kFirstWindowId  = "00000000-0000-4000-8000-000000000000";
    constexpr std::string_view kSecondWindowId = "00000000-0000-4000-8000-000000000001";

    std::string                FocusWindow(std::string_view id) {
        return "command focus --container-id " + std::string(id);
    }

} // namespace

TEST(GlazeWm, ClicksAlongTheBarSendEachItemsCommandInOrder) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    const integration::RunningBar bar("clicks", kConfig, kStyle);

    EXPECT_THAT(bar.CommandsFromSweep(),
                ElementsAre("command focus --workspace 1", "command focus --workspace 2", FocusWindow(kFirstWindowId), FocusWindow(kSecondWindowId),
                            "command focus --next-workspace", "command toggle-tiling-direction"));
}

TEST(GlazeWm, TakesClicksAgainOnceARestartedServerIsBack) {
    integration::RunningBar bar("reconnect", kConfig, kStyle);
    bar.StopServer();
    EXPECT_TRUE(integration::Eventually([&bar] { return !bar.LogLines("event=disconnected").empty(); }, std::chrono::seconds(5)));

    bar.StartServer();
    EXPECT_TRUE(integration::Eventually([&bar] { return bar.LogLines("event=connected").size() == 2; }, std::chrono::seconds(15)));
    EXPECT_THAT(bar.CommandsFromSweep(),
                ElementsAre("command focus --workspace 1", "command focus --workspace 2", "command focus --next-workspace", "command toggle-tiling-direction"));
}
