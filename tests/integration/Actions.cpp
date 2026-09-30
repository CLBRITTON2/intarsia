#include "Harness.hpp"
#include "platform/Text.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
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
                           integration::LuaString(ToUtf8(integration::ArgvRecorderPath().wstring())), integration::LuaString(ToUtf8(record.wstring())));
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
#launch { color: #27ae60; }
)";

} // namespace

TEST(Actions, AClickStartsItsProgramWithEachArgumentAsWritten) {
    const std::filesystem::path record = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    integration::RunningBar     bar(kTestName, ClickConfig(record), kStyle);
    std::optional<double>       column;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            column = bar.ColumnOf(kTextColor);
            return column.has_value();
        },
        kLimit))
        << "the module never drew in its color";

    bar.ClickAt(static_cast<std::int32_t>(std::lround(column.value_or(0))));
    ASSERT_TRUE(integration::Eventually([&] { return std::filesystem::exists(record) && !integration::ReadLines(record).empty(); }, kLimit))
        << "the click started no recorder within " << kLimit.count() << " s (" << record.string() << ")";
    integration::Log("argv_recorded", std::format("path=\"{}\" lines={}", record.string(), integration::ReadLines(record).size()));
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("plain", "two words", R"(a "quoted" one)", R"(C:\dir\)", ""));
}
