#include "Harness.hpp"
#include "platform/Errors.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellscalingapi.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using testing::HasSubstr;

namespace {

    // left and right are the Lua list contents of those sections.
    std::string OneModuleConfig(std::string_view left, std::string_view right) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ {} }},
    modules_center = {{}},
    modules_right = {{ {} }},
    layouts = {{}},
    modules = {{ mark = {{ type = "divider" }} }},
}}
)",
                           left, right);
    }

    constexpr std::uint32_t kGreen  = 0x27AE60;
    constexpr std::uint32_t kOrange = 0xF39C12;
    constexpr std::uint32_t kBlue   = 0x2E86C1;
    constexpr auto          kDrawn  = std::chrono::seconds(5);

    std::string             StyleWithRules(std::string_view rules) {
        return std::string(R"(
* { font-family: "Segoe UI"; font-size: 13px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
divider { width: 12px; height: 60%; }
)") + std::string(rules);
    }

    // The primary monitor's width in DIPs, as the bar measures it against min_length.
    float PrimaryLengthDips() {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HMONITOR              primary  = MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO           info{};
        info.cbSize      = sizeof(info);
        const BOOL found = GetMonitorInfoW(primary, &info);
        SetThreadDpiAwarenessContext(previous);
        if (!found) {
            ThrowLastError("GetMonitorInfoW(primary)");
        }
        UINT dpiX = 0;
        UINT dpiY = 0;
        ThrowIfFailed(GetDpiForMonitor(primary, MDT_EFFECTIVE_DPI, &dpiX, &dpiY), "GetDpiForMonitor(primary)");
        return static_cast<float>(info.rcMonitor.right - info.rcMonitor.left) * static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpiX);
    }

    // The top-level lists show low, a layout the primary monitor just reaches shows reached, and one a DIP past it
    // shows beyond.
    std::string LayoutsConfig(std::uint32_t reachedLength) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ "low" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{
        {{ min_length = {}, modules_left = {{ "reached" }}, modules_center = {{}}, modules_right = {{}} }},
        {{ min_length = {}, modules_left = {{ "beyond" }}, modules_center = {{}}, modules_right = {{}} }},
    }},
    modules = {{ low = {{ type = "divider" }}, reached = {{ type = "divider" }}, beyond = {{ type = "divider" }} }},
}}
)",
                           reachedLength, reachedLength + 1);
    }

} // namespace

TEST(Config, RedrawsAModuleSavedIntoAnotherSection) {
    integration::RunningBar bar("config-move", OneModuleConfig(R"("mark")", ""), StyleWithRules("#mark { color: #27ae60; }"));
    std::optional<double>   left;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            left = bar.ColumnOf(kGreen);
            return left.has_value();
        },
        kDrawn))
        << "the module never drew in the left section";
    RECT client{};
    GetClientRect(bar.PrimaryBar(), &client);
    const double middle = static_cast<double>(client.right) / 2;
    integration::Log("module_column", std::format("section=left column={:.1f} bar_width={}", left.value_or(-1), client.right));
    EXPECT_LT(left.value_or(middle), middle);

    bar.SaveConfig(OneModuleConfig("", R"("mark")"));
    std::optional<double> right;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            right = bar.ColumnOf(kGreen);
            return right.value_or(0) > middle;
        },
        kDrawn))
        << "the module stayed at column " << right.value_or(-1) << " after the save moved it right";
    integration::Log("module_column", std::format("section=right column={:.1f}", right.value_or(-1)));
}

TEST(Config, DrawsTheColorOfASavedStyle) {
    integration::RunningBar bar("config-style", OneModuleConfig(R"("mark")", ""), StyleWithRules("#mark { color: #27ae60; }"));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn)) << "the module never drew in the first style's color";

    bar.SaveStyle(StyleWithRules("#mark { color: #f39c12; }"));
    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the saved style's color never drew";
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kDrawn)) << "the first style's color still drew";
}

TEST(Config, EndsWithExitCode1AfterAnInvalidSave) {
    integration::RunningBar bar("config-invalid", OneModuleConfig(R"("mark")", ""), StyleWithRules("#mark { color: #27ae60; }"));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    bar.SaveConfig(OneModuleConfig(R"("mark", "missing")", ""));
    ASSERT_TRUE(integration::Eventually([&] { return !bar.LogLines("event=fatal").empty(); }, kDrawn)) << "the bar logged no fatal error after the invalid save";
    const std::vector<std::string> fatal = bar.LogLines("event=fatal");
    integration::Log("bar_logged", std::format("text=event=fatal lines={}", fatal.size()));
    EXPECT_THAT(fatal.front(), HasSubstr("missing"));
    EXPECT_EQ(bar.PrimaryBar(), nullptr) << "a bar still showed after the fatal error";
    EXPECT_EQ(bar.Close(), std::optional<std::uint32_t>(1));
}

TEST(Config, ShowsTheLastLayoutTheMonitorsLengthReaches) {
    const float                   length  = PrimaryLengthDips();
    const auto                    reached = static_cast<std::uint32_t>(std::floor(length));
    const integration::RunningBar bar("config-layouts", LayoutsConfig(reached),
                                      StyleWithRules("#low { color: #27ae60; } #reached { color: #f39c12; } #beyond { color: #2e86c1; }"));
    integration::Log("layouts", std::format("primary_length_dips={:.2f} reached_min_length={} beyond_min_length={}", length, reached, reached + 1));

    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the layout the monitor reaches never drew";
    EXPECT_EQ(bar.PixelsOf(kGreen), size_t{0}) << "the top-level lists drew";
    EXPECT_EQ(bar.PixelsOf(kBlue), size_t{0}) << "the layout past the monitor's length drew";
}
