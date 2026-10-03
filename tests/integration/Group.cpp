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
                           integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(record));
    }

    // No transitions, so a fold is done on the next frame.
    constexpr std::string_view kStyle = R"(
bar:
  font: Segoe UI
  size: 24
  color: #e0def4
section:
  margin: 4
  padding: 3
  background: #191724
more:
  background: #8e44ad
  padding: 0 8
member:
  background: #27ae60
)";

} // namespace

TEST(Group, UnfoldsItsMembersOnAClickAndTakesNoClickForThemWhileFolded) {
    const std::filesystem::path       record = integration::WorkDirectoryPath(kTestName) / "argv.txt";
    const integration::RunningBar     bar(kTestName, GroupConfig(record), kStyle);
    const std::optional<std::int32_t> toggle = bar.DrawnColumn(kGroupColor, kLimit);
    if (!toggle.has_value()) {
        FAIL() << "the folded group never drew";
    }
    EXPECT_EQ(bar.PixelCount(kMemberColor), size_t{0}) << "a member drew while the group starts folded";

    bar.ClickAt(*toggle);
    const std::optional<std::int32_t> member = bar.DrawnColumn(kMemberColor, kLimit);
    if (!member.has_value()) {
        FAIL() << "a click on the toggle never unfolded the member";
    }
    EXPECT_GT(*member, *toggle) << "the member did not unfold after the toggle";

    bar.ClickAt(*toggle);
    ASSERT_TRUE(bar.StopsDrawing(kMemberColor, kLimit)) << "a second click on the toggle left the member shown";
    bar.ClickAt(*member);
    integration::PumpFor(std::chrono::seconds(1));
    EXPECT_FALSE(std::filesystem::exists(record)) << "a click where the folded member was ran its action";

    bar.ClickAt(*toggle);
    ASSERT_TRUE(bar.Draws(kMemberColor, kLimit)) << "a third click on the toggle never unfolded the member";
    bar.ClickAt(*member);
    ASSERT_TRUE(integration::Eventually([&] { return !integration::RecordedLines(record).empty(); }, kLimit))
        << "a click on the unfolded member ran no action (" << record.string() << ")";
    EXPECT_THAT(integration::ReadLines(record), ElementsAre("member"));
}
