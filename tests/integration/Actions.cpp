#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <string>
#include <string_view>

using testing::ElementsAre;

namespace {

    constexpr std::string_view kClickTestName  = "actions-click";
    constexpr std::string_view kInputsTestName = "input-buttons";
    constexpr std::uint32_t    kTextColor      = 0x27AE60;
    constexpr auto             kLimit          = std::chrono::seconds(5);

    // Arguments that need quoting on a command line: a space, quotes, a trailing backslash and an empty one.
    std::string ClickConfig(const std::filesystem::path& record) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40, monitors = {{ "*" }} }},
    window_manager = "glazewm",
    modules_left = {{ "launch" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{
        launch = {{ type = "clock", format = "WWWW", icon = "", interval = 1,
                   on_click = {{ {}, {}, "plain", "two words", [[a "quoted" one]], [[C:\dir\]], "" }} }},
    }},
}}
)",
                           integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(record));
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kClickStyle = R"(
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
launch {
  color: #27ae60
}
)";

    // A bottom bar, so a wheel message's screen point lies far from the same client point.
    std::string InputsConfig(const std::filesystem::path& record) {
        const std::string recorder = integration::LuaPath(integration::ArgvRecorderPath());
        const std::string file     = integration::LuaPath(record);
        return std::format(R"(
return {{
    bar = {{ edge = "bottom", layer = "top", space = "share", thickness = 40, monitors = {{ "*" }} }},
    window_manager = "glazewm",
    modules_left = {{ "probe" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{
        probe = {{ type = "clock", format = "WWWW", icon = "", interval = 1,
                  on_right_click = {{ {0}, {1}, "right" }},
                  on_middle_click = {{ {0}, {1}, "middle" }},
                  on_scroll_up = {{ {0}, {1}, "up" }},
                  on_scroll_down = {{ {0}, {1}, "down" }} }},
    }},
}}
)",
                           recorder, file);
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kInputsStyle = R"(
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
probe {
  color: #27ae60
}
)";

    // Sends one input and waits until the record holds lines lines, so each action's line lands in order.
    bool RecordsAfter(const std::filesystem::path& record, size_t lines, const std::function<void()>& send) {
        send();
        const bool recorded = integration::Eventually([&] { return integration::RecordedLines(record).size() >= lines; }, kLimit);
        integration::Log("argv_recorded", std::format("path={} lines={} expected={}", integration::LogValue(record.string()), integration::RecordedLines(record).size(), lines));
        return recorded;
    }

} // namespace

TEST(Actions, AClickStartsItsProgramWithEachArgumentAsWritten) {
    const std::filesystem::path   record = integration::WorkDirectoryPath(kClickTestName) / "argv.txt";
    const integration::RunningBar bar(kClickTestName, ClickConfig(record), kClickStyle);
    bar.ClickAt(bar.DrawnColumn(kTextColor, kLimit));
    ASSERT_TRUE(integration::Eventually([&] { return !integration::RecordedLines(record).empty(); }, kLimit))
        << "the click started no recorder within " << kLimit.count() << " s (" << record.string() << ")";
    integration::Log("argv_recorded", std::format("path={} lines={}", integration::LogValue(record.string()), integration::ReadLines(record).size()));
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("plain", "two words", R"(a "quoted" one)", R"(C:\dir\)", ""));
}

TEST(Input, RightMiddleAndWheelInputsEachRunTheirAction) {
    const std::filesystem::path record = integration::WorkDirectoryPath(kInputsTestName) / "argv.txt";
    integration::RunningBar     bar(kInputsTestName, InputsConfig(record), kInputsStyle);
    const std::int32_t          at = bar.DrawnColumn(kTextColor, kLimit);

    EXPECT_TRUE(RecordsAfter(record, 1, [&] { bar.ButtonUpAt(WM_RBUTTONUP, at); }));
    EXPECT_TRUE(RecordsAfter(record, 2, [&] { bar.ButtonUpAt(WM_MBUTTONUP, at); }));
    EXPECT_TRUE(RecordsAfter(record, 3, [&] { bar.WheelAt(at, static_cast<std::int16_t>(WHEEL_DELTA)); }));
    EXPECT_TRUE(RecordsAfter(record, 4, [&] { bar.WheelAt(at, static_cast<std::int16_t>(-WHEEL_DELTA)); }));
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("right", "middle", "up", "down"));
}
