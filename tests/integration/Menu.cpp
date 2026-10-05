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

    constexpr std::uint32_t kTextColor = 0x27AE60;
    constexpr std::uint32_t kMenuColor = 0x8E44AD;
    constexpr auto          kLimit     = std::chrono::seconds(5);

    // A command that records each run in listing and prints two entries, ids "one" and "two".
    std::string ListingCommand(const std::filesystem::path& listing) {
        return std::format("{{ {}, {}, {} }}", integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(listing),
                           integration::LuaString(R"({"items":[{"id":"one","text":"One"},{"id":"two","text":"Two"}]})"));
    }

    // A clock whose click lists command's entries in a menu and records the picked id in selected.
    std::string MenuConfig(std::string_view command, const std::filesystem::path& selected) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ "probe" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{
        probe = {{ type = "clock", format = "WWWW", icon = "", interval = 1,
                  on_click = {{ menu = {{ command = {}, timeout = 5, on_select = {{ {}, {} }} }} }} }},
    }},
}}
)",
                           command, integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(selected));
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kStyle = R"(
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
menu {
  background: #8e44ad
}
)";

    bool                       MenuShown(const integration::RunningBar& bar) {
        const bool shown = integration::Eventually([&] { return bar.Menu() != nullptr; }, kLimit);
        integration::Log("menu_shown", std::format("shown={}", shown));
        return shown;
    }

    bool MenuHidden(const integration::RunningBar& bar) {
        const bool hidden = integration::Eventually([&] { return bar.Menu() == nullptr; }, kLimit);
        integration::Log("menu_hidden", std::format("hidden={}", hidden));
        return hidden;
    }

} // namespace

TEST(Menu, PicksTheEntryTheKeysHighlightAndRunsOnSelectWithItsId) {
    constexpr std::string_view        kTestName = "menu-pick";
    const std::filesystem::path       work      = integration::WorkDirectoryPath(kTestName);
    const std::filesystem::path       selected  = work / "selected.txt";
    const integration::RunningBar     bar(kTestName, MenuConfig(ListingCommand(work / "listing.txt"), selected), kStyle);
    const std::optional<std::int32_t> column = bar.DrawnColumn(kTextColor, kLimit);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }

    bar.ClickAt(*column);
    ASSERT_TRUE(MenuShown(bar)) << "the click showed no menu";
    HWND menu = bar.Menu();
    EXPECT_TRUE(integration::Eventually([&] { return integration::CountPixels(integration::CaptureScreenOver(menu), kMenuColor) > 0; }, kLimit))
        << "the menu never drew its background";

    integration::SendKey(menu, VK_DOWN);
    integration::SendKey(menu, VK_DOWN);
    integration::SendKey(menu, VK_RETURN);
    EXPECT_TRUE(MenuHidden(bar)) << "picking an entry left the menu shown";
    ASSERT_TRUE(integration::Eventually([&] { return !integration::RecordedLines(selected).empty(); }, kLimit)) << "no pick reached on_select (" << selected.string() << ")";
    EXPECT_THAT(integration::ReadLines(selected), ElementsAre("two"));
}

TEST(Menu, EscapeAndASecondClickOnItsModuleEachCloseIt) {
    constexpr std::string_view        kTestName = "menu-close";
    const std::filesystem::path       work      = integration::WorkDirectoryPath(kTestName);
    const std::filesystem::path       listing   = work / "listing.txt";
    const std::filesystem::path       selected  = work / "selected.txt";
    const integration::RunningBar     bar(kTestName, MenuConfig(ListingCommand(listing), selected), kStyle);
    const std::optional<std::int32_t> column = bar.DrawnColumn(kTextColor, kLimit);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }

    bar.ClickAt(*column);
    ASSERT_TRUE(MenuShown(bar)) << "the first click showed no menu";
    integration::SendKey(bar.Menu(), VK_ESCAPE);
    EXPECT_TRUE(MenuHidden(bar)) << "escape left the menu shown";

    bar.ClickAt(*column);
    ASSERT_TRUE(MenuShown(bar)) << "a click after escape showed no menu";
    bar.ClickAt(*column);
    EXPECT_TRUE(MenuHidden(bar)) << "a second click on the menu's module left it shown";

    // Each showing runs the command once, and the closing click runs it no more.
    integration::PumpFor(std::chrono::seconds(1));
    EXPECT_EQ(integration::RecordedLines(listing).size(), size_t{2});
    EXPECT_EQ(integration::RecordedLines(selected).size(), size_t{0});
}

TEST(Menu, LogsACommandThatFails) {
    constexpr std::string_view        kTestName = "menu-failed";
    const std::filesystem::path       selected  = integration::WorkDirectoryPath(kTestName) / "selected.txt";
    const std::string                 failing   = std::format(R"({{ {}, "/c", "exit 3" }})", integration::LuaString(R"(C:\Windows\System32\cmd.exe)"));
    const integration::RunningBar     bar(kTestName, MenuConfig(failing, selected), kStyle);
    const std::optional<std::int32_t> column = bar.DrawnColumn(kTextColor, kLimit);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }

    bar.ClickAt(*column);
    EXPECT_TRUE(integration::Eventually([&] { return !bar.Events("menu_failed").empty(); }, kLimit)) << "the failed command was never logged";
    EXPECT_TRUE(MenuShown(bar)) << "the failure showed no menu row";
}
