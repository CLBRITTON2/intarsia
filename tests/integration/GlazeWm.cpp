#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
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

    constexpr std::string_view kWindowsConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "apps" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { apps = { type = "windows" } },
}
)";

    constexpr std::uint32_t    kFocusedColor = 0x2E86C1;

    constexpr std::string_view kFocusedStyle = R"(
* { font-family: "Segoe UI"; font-size: 13px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
.item { padding: 2px 6px; margin: 0 1px; }
.item.focused { background: #2e86c1; }
)";

    constexpr std::string_view kWorkspacesConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "workspaces" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { workspaces = { type = "workspaces" } },
}
)";

    constexpr std::uint32_t    kTextColor  = 0xE0DEF4;
    constexpr std::uint32_t    kAlertColor = 0xE74C3C;

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kAlertStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
.alert { color: #e74c3c; }
)";

    // The mean column of the pixels in rgb, or nullopt when there are none.
    std::optional<double> MeanColumn(const integration::Capture& capture, std::uint32_t rgb) {
        double sum   = 0;
        size_t count = 0;
        for (size_t index = 0; index < capture.pixels.size(); ++index) {
            if (capture.pixels[index] == rgb) {
                sum += static_cast<double>(index % static_cast<size_t>(capture.width));
                ++count;
            }
        }
        if (count == 0) {
            return std::nullopt;
        }
        return sum / static_cast<double>(count);
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
    integration::Log("bar_logged", std::format("text=event=disconnected lines={}", bar.LogLines("event=disconnected").size()));

    bar.StartServer();
    EXPECT_TRUE(integration::Eventually([&bar] { return bar.LogLines("event=connected").size() == 2; }, std::chrono::seconds(15)));
    integration::Log("bar_logged", std::format("text=event=connected lines={}", bar.LogLines("event=connected").size()));
    EXPECT_THAT(bar.CommandsFromSweep(),
                ElementsAre("command focus --workspace 1", "command focus --workspace 2", "command focus --next-workspace", "command toggle-tiling-direction"));
}

TEST(GlazeWm, DrawsTheAlertColorWithinTwoSecondsOfTheServerClosing) {
    integration::RunningBar bar("disconnect", kWorkspacesConfig, kAlertStyle);
    ASSERT_TRUE(integration::Eventually([&bar] { return integration::CountPixels(integration::CaptureScreenOver(bar.PrimaryBar()), kTextColor) > 0; }, std::chrono::seconds(5)))
        << "the connected workspaces never drew in the text color";
    const size_t connectedPixels = integration::CountPixels(integration::CaptureScreenOver(bar.PrimaryBar()), kAlertColor);
    integration::Log("pixels_counted", std::format("color=#{:06x} count={} while=connected", kAlertColor, connectedPixels));
    EXPECT_EQ(connectedPixels, 0U);

    bar.StopServer();
    size_t alertPixels = 0;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            alertPixels = integration::CountPixels(integration::CaptureScreenOver(bar.PrimaryBar()), kAlertColor);
            return alertPixels > 0;
        },
        std::chrono::seconds(2)))
        << "no pixel drew in the alert color within 2 s of the server closing";
    integration::Log("pixels_counted", std::format("color=#{:06x} count={} while=disconnected", kAlertColor, alertPixels));
}

TEST(GlazeWm, MovesTheFocusedColorToTheWindowAPushedFocusNames) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    integration::RunningBar       bar("focus", kWindowsConfig, kFocusedStyle);

    std::optional<double>         firstColumn;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            firstColumn = MeanColumn(integration::CaptureScreenOver(bar.PrimaryBar()), kFocusedColor);
            return firstColumn.has_value();
        },
        std::chrono::seconds(5)))
        << "the first window's item never drew in the focused color";
    integration::Log("focused_color_found", std::format("color=#{:06x} mean_column={:.1f}", kFocusedColor, firstColumn.value_or(-1)));

    bar.PushFocus(1);
    std::optional<double> secondColumn;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            secondColumn = MeanColumn(integration::CaptureScreenOver(bar.PrimaryBar()), kFocusedColor);
            return firstColumn.has_value() && secondColumn.has_value() && *secondColumn > *firstColumn;
        },
        std::chrono::seconds(5)))
        << "the focused color stayed at column " << firstColumn.value_or(-1) << ", now " << secondColumn.value_or(-1);
    integration::Log("focused_color_found", std::format("color=#{:06x} mean_column={:.1f}", kFocusedColor, secondColumn.value_or(-1)));
}
