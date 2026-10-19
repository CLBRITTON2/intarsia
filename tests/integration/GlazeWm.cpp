#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using testing::ElementsAre;
using testing::ElementsAreArray;

namespace {

    // Every module that sends GlazeWM a command on a click, one per place along the bar. The test server shows
    // workspaces 1 and 2 on the primary monitor, 2 displayed and holding the test's windows.
    std::string ClicksConfig(std::string_view edge) {
        return std::string(R"(
return {
    bar = { edge = ")") +
            std::string(edge) +
            R"(", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
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
    }

    constexpr std::string_view kStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  padding: 0 8
  item {
    padding: 2 6
    margin: 0 1
    focused {
      color: #191724
      background: #9ccfd8
    }
  }
}
)";

    // The ids the test server gives the windows it lists, in its order (tests/support/GlazeWmServer.cpp).
    constexpr std::string_view kFirstWindowId = "00000000-0000-4000-8000-000000000000";
    constexpr std::string_view kSecondWindowId = "00000000-0000-4000-8000-000000000001";

    std::string FocusWindow(std::string_view id) {
        return "command focus --container-id " + std::string(id);
    }

    // What a sweep along ClicksConfig's bar sends, in order, while the test's two windows are open.
    std::vector<std::string> EveryClickCommand() {
        return {"command focus --workspace 1", "command focus --workspace 2",    FocusWindow(kFirstWindowId),
                FocusWindow(kSecondWindowId),  "command focus --next-workspace", "command toggle-tiling-direction"};
    }

    constexpr std::string_view kWorkspacesConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "workspaces" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { workspaces = { type = "workspaces" } },
}
)";

    constexpr std::uint32_t kFocusedColor = 0x2E86C1;

    // The test server shows the binding mode "resize" and the vertical direction while the focus index is odd.
    constexpr std::string_view kModesConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "modes", "tiling" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = {
        modes = { type = "binding_mode" },
        tiling = { type = "tiling_direction", horizontal = "WWWW", vertical = "W" },
    },
}
)";

    constexpr std::uint32_t kModeColor = 0xF39C12;
    constexpr std::uint32_t kTilingColor = 0x2E86C1;

    constexpr std::string_view kModesStyle = R"(
bar {
  font: Segoe UI
  size: 24
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
modes {
  item {
    background: #f39c12
  }
}
tiling {
  background: #2e86c1
}
)";

} // namespace

TEST(GlazeWm, ClicksAlongTheBarSendEachItemsCommandInOrder) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    const integration::RunningBar bar("clicks", ClicksConfig("top"), kStyle);

    EXPECT_THAT(bar.CommandsFromSweep(), ElementsAreArray(EveryClickCommand()));
}

TEST(GlazeWm, ClicksDownASideBarSendEachItemsCommandInOrder) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    const integration::RunningBar bar("side-clicks", ClicksConfig("left"), kStyle);

    EXPECT_THAT(bar.CommandsFromSweep(), ElementsAreArray(EveryClickCommand()));
}

namespace {

    // A tiling module the rejecting server never gives a direction, styled so any drawing of it shows.
    constexpr std::string_view kRejectedStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  padding: 0 8
  item {
    padding: 2 6
    margin: 0 1
  }
}
tiling {
  background: #2e86c1
}
)";

} // namespace

TEST(GlazeWm, StaysConnectedThroughARejectedCommandAndDirectionQuery) {
    const integration::RunningBar bar("rejected", ClicksConfig("top"), kRejectedStyle, integration::GlazeWmTestServer(L"rejecting"));

    // The direction has no text and takes no room, so no click reaches the toggle.
    EXPECT_THAT(bar.CommandsFromSweep(), ElementsAre("command focus --workspace 1", "command focus --workspace 2", "command focus --next-workspace"));
    EXPECT_TRUE(integration::Eventually([&bar] { return !bar.Events("command_failed").empty(); }, integration::kLimit)) << "the rejected focus --workspace 1 was never logged";
    for (const std::string& line : bar.Events("command_failed")) {
        EXPECT_NE(line.find("focus --workspace 1"), std::string::npos) << line;
    }
    EXPECT_THAT(bar.Events("disconnected"), testing::IsEmpty());
    EXPECT_EQ(bar.PixelCount(kTilingColor), size_t{0});
}

TEST(GlazeWm, RefusesAServerThatIsNotGlazeWmAndSendsItNothing) {
    const std::filesystem::path impostor = integration::NewWorkDirectory("impostor-server") / "impostor.exe";
    std::filesystem::copy_file(integration::GlazeWmTestServer(L"integration").program, impostor, std::filesystem::copy_options::overwrite_existing);
    const integration::RunningBar bar("impostor", ClicksConfig("top"), kStyle,
                                      integration::TestServer{
                                          .program = impostor,
                                          .scenario = L"integration",
                                          .readyEvent = "disconnected",
                                      });

    const std::vector<std::string> refused = bar.Events("disconnected");
    ASSERT_FALSE(refused.empty());
    EXPECT_NE(refused.front().find("not glazewm.exe"), std::string::npos) << refused.front();

    const std::uint32_t clicks = integration::SweepClicks(bar.PrimaryBar());
    EXPECT_TRUE(integration::Eventually([&bar] { return !bar.Events("command_dropped").empty(); }, std::chrono::seconds(2)))
        << "the clock's click was not dropped for want of a connection";
    // Long enough for a command sent anyway to reach the server's output.
    integration::PumpFor(std::chrono::seconds(1));
    integration::Log("sweep", std::format("clicks={} commands={}", clicks, bar.Commands().size()));
    EXPECT_THAT(bar.Commands(), testing::IsEmpty());
    EXPECT_THAT(bar.Events("connected"), testing::IsEmpty());
}

namespace {

    std::optional<std::int32_t> TilingSpan(const integration::RunningBar& bar) {
        HWND window = bar.PrimaryBar();
        if (window == nullptr) {
            return std::nullopt;
        }
        return integration::ColumnSpan(integration::CaptureScreenOver(window), kTilingColor);
    }

} // namespace

TEST(GlazeWm, ShowsABindingModeOnlyWhileItIsActiveAndTheConfiguredTextForTheTilingDirection) {
    integration::RunningBar bar("binding-mode", kModesConfig, kModesStyle);
    ASSERT_TRUE(bar.Draws(kTilingColor, integration::kLimit)) << "the tiling direction never drew, so the bar has no answers yet";
    EXPECT_EQ(bar.PixelCount(kModeColor), size_t{0}) << "a binding mode drew while none is active";
    std::optional<std::int32_t> horizontal;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            horizontal = TilingSpan(bar);
            return horizontal.has_value();
        },
        integration::kLimit))
        << "the horizontal direction never drew";

    bar.PushFocus(1);
    EXPECT_TRUE(bar.Draws(kModeColor, integration::kLimit)) << "the active binding mode never drew";
    std::optional<std::int32_t> vertical;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            vertical = TilingSpan(bar);
            return horizontal.has_value() && vertical.has_value() && *vertical < *horizontal;
        },
        integration::kLimit))
        << "the direction's box stayed " << horizontal.value_or(-1) << " px wide for the shorter vertical text, now " << vertical.value_or(-1);
    integration::Log("tiling_spans", std::format("horizontal={} vertical={}", horizontal.value_or(-1), vertical.value_or(-1)));

    bar.PushFocus(2);
    EXPECT_TRUE(bar.StopsDrawing(kModeColor, integration::kLimit)) << "the binding mode still drew once it ended";
}

namespace {

    constexpr std::uint32_t kEmptyColor = 0xF39C12;

    constexpr std::string_view kStatesStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  item {
    padding: 2 6
    margin: 0 1
    empty {
      background: #f39c12
    }
    focused {
      background: #2e86c1
    }
  }
}
)";

} // namespace

TEST(GlazeWm, StylesAnEmptyWorkspaceApartFromTheFocusedOne) {
    integration::RunningBar bar("workspace-states", kWorkspacesConfig, kStatesStyle);
    std::optional<double> empty;
    std::optional<double> focused;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            empty = integration::MeanColumn(capture, kEmptyColor);
            focused = integration::MeanColumn(capture, kFocusedColor);
            return empty.has_value() && focused.has_value() && *empty < *focused;
        },
        integration::kLimit))
        << "workspace 1 (empty) did not draw left of workspace 2 (focused): empty column " << empty.value_or(-1) << ", focused column " << focused.value_or(-1);
    integration::Log("workspace_columns", std::format("empty={:.1f} focused={:.1f}", empty.value_or(-1), focused.value_or(-1)));
}

namespace {

    constexpr std::uint32_t kTextColor = 0xE0DEF4;
    constexpr std::uint32_t kAlertColor = 0xE74C3C;

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kAlertStyle = R"(
bar {
  font: Segoe UI
  size: 24
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  alert {
    color: #e74c3c
  }
}
)";

} // namespace

TEST(GlazeWm, DrawsTheAlertColorWithinTwoSecondsOfTheServerClosingAndTakesClicksAgainOnceItIsBack) {
    integration::RunningBar bar("reconnect", ClicksConfig("top"), kAlertStyle);
    // Until its first connection's answers arrive, the bar draws "not connected yet" in the alert color too.
    size_t connectedPixels = 0;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            connectedPixels = integration::CountPixels(capture, kAlertColor);
            return integration::CountPixels(capture, kTextColor) > 0 && connectedPixels == 0;
        },
        integration::kLimit))
        << "the connected workspaces never drew in the text color alone, " << connectedPixels << " alert pixels";
    integration::Log("pixels_counted", std::format("color=#{:06x} count={} while=connected", kAlertColor, connectedPixels));

    bar.StopServer();
    EXPECT_TRUE(bar.Draws(kAlertColor, std::chrono::seconds(2))) << "no pixel drew in the alert color within 2 s of the server closing";
    EXPECT_TRUE(integration::Eventually([&bar] { return !bar.Events("disconnected").empty(); }, integration::kLimit));
    integration::Log("bar_logged", std::format("event=disconnected lines={}", bar.Events("disconnected").size()));

    bar.StartServer();
    EXPECT_TRUE(integration::Eventually([&bar] { return bar.Events("connected").size() == 2; }, std::chrono::seconds(15)));
    integration::Log("bar_logged", std::format("event=connected lines={}", bar.Events("connected").size()));
    EXPECT_THAT(bar.CommandsFromSweep(),
                ElementsAre("command focus --workspace 1", "command focus --workspace 2", "command focus --next-workspace", "command toggle-tiling-direction"));
}

namespace {

    constexpr std::string_view kWindowsConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "apps" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { apps = { type = "windows" } },
}
)";

    constexpr std::string_view kFocusedStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  item {
    padding: 2 6
    margin: 0 1
    focused {
      background: #2e86c1
    }
  }
}
)";

} // namespace

TEST(GlazeWm, MovesTheFocusedColorToTheWindowAPushedFocusNames) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    integration::RunningBar bar("focus", kWindowsConfig, kFocusedStyle);

    const std::int32_t firstColumn = bar.DrawnColumn(kFocusedColor, integration::kLimit);
    integration::Log("focused_color_found", std::format("color=#{:06x} column={}", kFocusedColor, firstColumn));

    bar.PushFocus(1);
    std::optional<double> secondColumn;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            secondColumn = bar.MeanColumnOf(kFocusedColor);
            return secondColumn.has_value() && *secondColumn > firstColumn;
        },
        integration::kLimit))
        << "the focused color stayed at column " << firstColumn << ", now " << secondColumn.value_or(-1);
    integration::Log("focused_color_found", std::format("color=#{:06x} mean_column={:.1f}", kFocusedColor, secondColumn.value_or(-1)));
}
