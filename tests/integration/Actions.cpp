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

using testing::ElementsAre;

namespace {

    constexpr std::string_view kTestName  = "actions-click";
    constexpr std::uint32_t    kTextColor = 0x27AE60;
    constexpr auto             kLimit     = std::chrono::seconds(5);

    // Arguments that need quoting on a command line: a space, quotes, a trailing backslash and an empty one.
    std::string ClickConfig(const std::filesystem::path& record) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
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
    constexpr std::string_view kStyle = R"(
bar:
  font: Segoe UI
  size: 24
  color: #e0def4
section:
  margin: 4
  padding: 3
  background: #191724
launch:
  color: #27ae60
)";

} // namespace

TEST(Actions, AClickStartsItsProgramWithEachArgumentAsWritten) {
    const std::filesystem::path       record = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    const integration::RunningBar     bar(kTestName, ClickConfig(record), kStyle);
    const std::optional<std::int32_t> column = bar.DrawnColumn(kTextColor, kLimit);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }

    bar.ClickAt(*column);
    ASSERT_TRUE(integration::Eventually([&] { return !integration::RecordedLines(record).empty(); }, kLimit))
        << "the click started no recorder within " << kLimit.count() << " s (" << record.string() << ")";
    integration::Log("argv_recorded", std::format("path=\"{}\" lines={}", record.string(), integration::ReadLines(record).size()));
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("plain", "two words", R"(a "quoted" one)", R"(C:\dir\)", ""));
}
