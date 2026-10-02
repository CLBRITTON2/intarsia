#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

using testing::ElementsAre;

namespace {

    constexpr std::string_view kTestName  = "input-buttons";
    constexpr std::uint32_t    kTextColor = 0x27AE60;
    constexpr auto             kLimit     = std::chrono::seconds(5);

    // A bottom bar, so a wheel message's screen point lies far from the same client point.
    std::string InputsConfig(const std::filesystem::path& record) {
        const std::string recorder = integration::LuaPath(integration::ArgvRecorderPath());
        const std::string file     = integration::LuaPath(record);
        return std::format(R"(
return {{
    bar = {{ edge = "bottom", layer = "top", space = "share", thickness = 40 }},
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
    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
#probe { color: #27ae60; }
)";

    // Sends one input and waits until the record holds lines lines, so each action's line lands in order.
    bool RecordsAfter(const std::filesystem::path& record, size_t lines, const std::function<void()>& send) {
        send();
        const bool recorded = integration::Eventually([&] { return integration::RecordedLines(record).size() >= lines; }, kLimit);
        integration::Log("argv_recorded", std::format("path=\"{}\" lines={} expected={}", record.string(), integration::RecordedLines(record).size(), lines));
        return recorded;
    }

} // namespace

TEST(Input, RightMiddleAndWheelInputsEachRunTheirAction) {
    const std::filesystem::path       record = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    integration::RunningBar           bar(kTestName, InputsConfig(record), kStyle);
    const std::optional<std::int32_t> column = bar.DrawnColumn(kTextColor, kLimit);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }
    const std::int32_t at = *column;

    EXPECT_TRUE(RecordsAfter(record, 1, [&] { bar.ButtonUpAt(WM_RBUTTONUP, at); }));
    EXPECT_TRUE(RecordsAfter(record, 2, [&] { bar.ButtonUpAt(WM_MBUTTONUP, at); }));
    EXPECT_TRUE(RecordsAfter(record, 3, [&] { bar.WheelAt(at, static_cast<std::int16_t>(WHEEL_DELTA)); }));
    EXPECT_TRUE(RecordsAfter(record, 4, [&] { bar.WheelAt(at, static_cast<std::int16_t>(-WHEEL_DELTA)); }));
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("right", "middle", "up", "down"));
}
