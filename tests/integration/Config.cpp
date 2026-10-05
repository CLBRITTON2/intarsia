#include "Harness.hpp"
#include "platform/Errors.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellscalingapi.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
    modules = {{ mark = {{ type = "clock", format = "WWWW", icon = "", interval = 60 }} }},
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
  padding: 4 8
}
)") + std::string(rules);
    }

    // The primary monitor's width in DIPs, as the bar measures it against min_length.
    float PrimaryLengthDips() {
        HMONITOR    primary = MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(primary, &info)) {
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
    modules = {{
        low = {{ type = "clock", format = "WWWW", icon = "", interval = 60 }},
        reached = {{ type = "clock", format = "WWWW", icon = "", interval = 60 }},
        beyond = {{ type = "clock", format = "WWWW", icon = "", interval = 60 }},
    }},
}}
)",
                           reachedLength, reachedLength + 1);
    }

    // mark records "mark" in record on a click.
    std::string ClickableConfig(const std::filesystem::path& record) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ "mark" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{ mark = {{ type = "clock", format = "WWWW", icon = "", interval = 60,
                         on_click = {{ {}, {}, "mark" }} }} }},
}}
)",
                           integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(record));
    }

} // namespace

TEST(Config, KeepsRunningWhenAnInvalidSaveIsSoonReplaced) {
    integration::RunningBar bar("config-resave", OneModuleConfig(R"("mark")", ""), StyleWithRules("mark {\n  background: #27ae60\n}\n"));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    // Cut short, as a reload can read a file an editor is still writing.
    const std::string valid = OneModuleConfig(R"("mark")", "");
    bar.SaveConfig(std::string_view(valid).substr(0, valid.size() / 2));
    ASSERT_TRUE(integration::Eventually([&] { return !bar.Events("config_invalid").empty(); }, kDrawn)) << "the cut-short save was not logged as invalid";
    bar.SaveStyle(StyleWithRules("mark {\n  background: #f39c12\n}\n"));
    bar.SaveConfig(valid);

    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the replacing save never drew";
    // Past the second an invalid file must stay unchanged before it ends the bar.
    integration::PumpFor(std::chrono::seconds(2));
    EXPECT_THAT(bar.Events("fatal"), testing::IsEmpty());
    EXPECT_NE(bar.PrimaryBar(), nullptr) << "the bar closed although a valid save replaced the invalid one";
}

TEST(Config, DrawsARightBorderPastTheBackground) {
    const integration::RunningBar bar("config-border", OneModuleConfig(R"("mark")", ""), StyleWithRules("mark {\n  background: #27ae60\n  border-right: 8 #2e86c1\n}\n"));
    std::optional<double>         background;
    std::optional<double>         border;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            background                         = integration::MeanColumn(capture, kGreen);
            border                             = integration::MeanColumn(capture, kBlue);
            return background.has_value() && border.has_value() && *border > *background;
        },
        kDrawn))
        << "the border did not draw right of the background: background column " << background.value_or(-1) << ", border column " << border.value_or(-1);
    integration::Log("border_columns", std::format("background={:.1f} border={:.1f}", background.value_or(-1), border.value_or(-1)));
}

TEST(Config, AClickOnAModulesMarginRunsNothing) {
    constexpr std::string_view  kTestName = "config-margin";
    const std::filesystem::path record    = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    // 40 DIPs of margin, so a click 10 pixels before the box lands in it at any scale.
    const integration::RunningBar bar(kTestName, ClickableConfig(record), StyleWithRules("mark {\n  background: #27ae60\n  margin: 0 40\n}\n"));
    std::optional<double>         middle;
    std::optional<std::int32_t>   span;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            middle                             = integration::MeanColumn(capture, kGreen);
            span                               = integration::ColumnSpan(capture, kGreen);
            return middle.has_value() && span.has_value();
        },
        kDrawn))
        << "the module never drew its background";
    const auto boxLeft = static_cast<std::int32_t>(std::lround(middle.value_or(0) - (static_cast<double>(span.value_or(0)) / 2)));
    integration::Log("module_box", std::format("left={} span={}", boxLeft, span.value_or(-1)));

    bar.ClickAt(boxLeft - 10);
    integration::PumpFor(std::chrono::seconds(1));
    EXPECT_FALSE(std::filesystem::exists(record)) << "a click in the margin ran the module's action";

    bar.ClickAt(static_cast<std::int32_t>(std::lround(middle.value_or(0))));
    EXPECT_TRUE(integration::Eventually([&] { return !integration::RecordedLines(record).empty(); }, kDrawn))
        << "a click on the box ran no action, so the margin click proves nothing";
}

TEST(Config, RedrawsAModuleSavedIntoAnotherSection) {
    integration::RunningBar bar("config-move", OneModuleConfig(R"("mark")", ""), StyleWithRules("mark {\n  background: #27ae60\n}\n"));
    std::optional<double>   left;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            left = bar.MeanColumnOf(kGreen);
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
            right = bar.MeanColumnOf(kGreen);
            return right.value_or(0) > middle;
        },
        kDrawn))
        << "the module stayed at column " << right.value_or(-1) << " after the save moved it right";
    integration::Log("module_column", std::format("section=right column={:.1f}", right.value_or(-1)));
}

TEST(Config, DrawsTheColorOfASavedStyle) {
    const integration::RunningBar bar("config-style", OneModuleConfig(R"("mark")", ""), StyleWithRules("mark {\n  background: #27ae60\n}\n"));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn)) << "the module never drew in the first style's color";

    bar.SaveStyle(StyleWithRules("mark {\n  background: #f39c12\n}\n"));
    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the saved style's color never drew";
    EXPECT_TRUE(bar.StopsDrawing(kGreen, kDrawn)) << "the first style's color still drew";
}

TEST(Config, EndsWithExitCode1AfterAnInvalidSave) {
    integration::RunningBar bar("config-invalid", OneModuleConfig(R"("mark")", ""), StyleWithRules("mark {\n  background: #27ae60\n}\n"));
    ASSERT_TRUE(bar.Draws(kGreen, kDrawn));

    bar.SaveConfig(OneModuleConfig(R"("mark", "missing")", ""));
    ASSERT_TRUE(integration::Eventually([&] { return !bar.Events("fatal").empty(); }, kDrawn)) << "the bar logged no fatal error after the invalid save";
    const std::vector<std::string> fatal = bar.Events("fatal");
    integration::Log("bar_logged", std::format("event=fatal lines={}", fatal.size()));
    EXPECT_THAT(fatal.front(), HasSubstr("missing"));
    EXPECT_EQ(bar.PrimaryBar(), nullptr) << "a bar still showed after the fatal error";
    EXPECT_EQ(bar.Close(), std::optional<std::uint32_t>(1));
}

TEST(Config, ShowsTheLastLayoutTheMonitorsLengthReaches) {
    const float                   length  = PrimaryLengthDips();
    const auto                    reached = static_cast<std::uint32_t>(std::floor(length));
    const integration::RunningBar bar("config-layouts", LayoutsConfig(reached),
                                      StyleWithRules("low {\n  background: #27ae60\n}\nreached {\n  background: #f39c12\n}\nbeyond {\n  background: #2e86c1\n}\n"));
    integration::Log("layouts", std::format("primary_length_dips={:.2f} reached_min_length={} beyond_min_length={}", length, reached, reached + 1));

    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the layout the monitor reaches never drew";
    EXPECT_EQ(bar.PixelCount(kGreen), size_t{0}) << "the top-level lists drew";
    EXPECT_EQ(bar.PixelCount(kBlue), size_t{0}) << "the layout past the monitor's length drew";
}
