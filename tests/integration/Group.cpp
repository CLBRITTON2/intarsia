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

    constexpr std::string_view kTestName    = "group-fold";
    constexpr std::uint32_t    kGroupColor  = 0x8E44AD;
    constexpr std::uint32_t    kMemberColor = 0x27AE60;
    constexpr auto             kLimit       = std::chrono::seconds(5);

    // A group whose one member records "member" in record on a click.
    std::string GroupConfig(const std::filesystem::path& record) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ "more" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{
        more = {{ type = "group", icon = "G", modules = {{ "member" }} }},
        member = {{ type = "clock", format = "WWWW", icon = "", interval = 60,
                   on_click = {{ {}, {}, "member" }} }},
    }},
}}
)",
                           integration::LuaString(ToUtf8(integration::ArgvRecorderPath().wstring())), integration::LuaString(ToUtf8(record.wstring())));
    }

    // No transitions, so a fold is done on the next frame.
    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
#more { background: #8e44ad; padding: 0 8px; }
#member { background: #27ae60; }
)";

    // The middle of the pixels drawn in rgb, or nullopt when none are drawn within kLimit.
    std::optional<std::int32_t> DrawnColumn(const integration::RunningBar& bar, std::uint32_t rgb) {
        std::optional<double> column;
        if (!integration::Eventually(
                [&] {
                    column = bar.ColumnOf(rgb);
                    return column.has_value();
                },
                kLimit)) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(std::lround(column.value_or(0)));
    }

    bool Recorded(const std::filesystem::path& record) {
        return std::filesystem::exists(record) && !integration::ReadLines(record).empty();
    }

} // namespace

TEST(Group, UnfoldsItsMembersOnAClickAndTakesNoClickForThemWhileFolded) {
    const std::filesystem::path       record = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    const integration::RunningBar     bar(kTestName, GroupConfig(record), kStyle);
    const std::optional<std::int32_t> toggle = DrawnColumn(bar, kGroupColor);
    if (!toggle.has_value()) {
        FAIL() << "the folded group never drew";
    }
    EXPECT_EQ(bar.PixelsOf(kMemberColor), size_t{0}) << "a member drew while the group starts folded";

    bar.ClickAt(*toggle);
    const std::optional<std::int32_t> member = DrawnColumn(bar, kMemberColor);
    if (!member.has_value()) {
        FAIL() << "a click on the toggle never unfolded the member";
    }
    EXPECT_GT(*member, *toggle) << "the member did not unfold after the toggle";

    bar.ClickAt(*toggle);
    ASSERT_TRUE(bar.StopsDrawing(kMemberColor, kLimit)) << "a second click on the toggle left the member shown";
    bar.ClickAt(*member);
    integration::Eventually([] { return false; }, std::chrono::seconds(1));
    EXPECT_FALSE(std::filesystem::exists(record)) << "a click where the folded member was ran its action";

    bar.ClickAt(*toggle);
    ASSERT_TRUE(bar.Draws(kMemberColor, kLimit)) << "a third click on the toggle never unfolded the member";
    bar.ClickAt(*member);
    ASSERT_TRUE(integration::Eventually([&] { return Recorded(record); }, kLimit)) << "a click on the unfolded member ran no action (" << record.string() << ")";
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("member"));
}
