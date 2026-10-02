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

    constexpr std::uint32_t kTextColor = 0x27AE60;
    constexpr std::uint32_t kMenuColor = 0x8E44AD;
    constexpr auto          kLimit     = std::chrono::seconds(5);

    std::string             LuaPath(const std::filesystem::path& path) {
        return integration::LuaString(ToUtf8(path.wstring()));
    }

    // A command that records each run in listing and prints two entries, ids "one" and "two".
    std::string ListingCommand(const std::filesystem::path& listing) {
        return std::format("{{ {}, {}, {} }}", LuaPath(integration::ArgvRecorderPath()), LuaPath(listing),
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
                           command, LuaPath(integration::ArgvRecorderPath()), LuaPath(selected));
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
#probe { color: #27ae60; }
menu { background: #8e44ad; }
)";

    // The middle of the module's text, or nullopt when it never draws.
    std::optional<std::int32_t> ProbeColumn(const integration::RunningBar& bar) {
        std::optional<double> column;
        if (!integration::Eventually(
                [&] {
                    column = bar.ColumnOf(kTextColor);
                    return column.has_value();
                },
                kLimit)) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(std::lround(column.value_or(0)));
    }

    bool MenuShown(const integration::RunningBar& bar) {
        const bool shown = integration::Eventually([&] { return bar.Menu() != nullptr; }, kLimit);
        integration::Log("menu_shown", std::format("shown={}", shown));
        return shown;
    }

    bool MenuHidden(const integration::RunningBar& bar) {
        const bool hidden = integration::Eventually([&] { return bar.Menu() == nullptr; }, kLimit);
        integration::Log("menu_hidden", std::format("hidden={}", hidden));
        return hidden;
    }

    size_t LinesIn(const std::filesystem::path& path) {
        return std::filesystem::exists(path) ? integration::ReadLines(path).size() : 0;
    }

} // namespace

TEST(Menu, PicksTheEntryTheKeysHighlightAndRunsOnSelectWithItsId) {
    constexpr std::string_view        kTestName = "menu-pick";
    const std::filesystem::path       work      = integration::WorkDirectoryPath(kTestName);
    const std::filesystem::path       selected  = work / "selected.txt";
    const integration::RunningBar     bar(kTestName, MenuConfig(ListingCommand(work / "listing.txt"), selected), kStyle);
    const std::optional<std::int32_t> column = ProbeColumn(bar);
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
    ASSERT_TRUE(integration::Eventually([&] { return LinesIn(selected) > 0; }, kLimit)) << "no pick reached on_select (" << selected.string() << ")";
    EXPECT_THAT(integration::ReadLines(selected), ElementsAre("two"));
}

TEST(Menu, EscapeAndASecondClickOnItsModuleEachCloseIt) {
    constexpr std::string_view        kTestName = "menu-close";
    const std::filesystem::path       work      = integration::WorkDirectoryPath(kTestName);
    const std::filesystem::path       listing   = work / "listing.txt";
    const std::filesystem::path       selected  = work / "selected.txt";
    const integration::RunningBar     bar(kTestName, MenuConfig(ListingCommand(listing), selected), kStyle);
    const std::optional<std::int32_t> column = ProbeColumn(bar);
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
    integration::Eventually([] { return false; }, std::chrono::seconds(1));
    EXPECT_EQ(LinesIn(listing), size_t{2});
    EXPECT_EQ(LinesIn(selected), size_t{0});
}

TEST(Menu, LogsACommandThatFails) {
    constexpr std::string_view        kTestName = "menu-failed";
    const std::filesystem::path       selected  = integration::WorkDirectoryPath(kTestName) / "selected.txt";
    const std::string                 failing   = std::format(R"({{ {}, "/c", "exit 3" }})", integration::LuaString(R"(C:\Windows\System32\cmd.exe)"));
    const integration::RunningBar     bar(kTestName, MenuConfig(failing, selected), kStyle);
    const std::optional<std::int32_t> column = ProbeColumn(bar);
    if (!column.has_value()) {
        FAIL() << "the module never drew in its color";
    }

    bar.ClickAt(*column);
    EXPECT_TRUE(integration::Eventually([&] { return !bar.LogLines("event=menu_failed").empty(); }, kLimit)) << "the failed command was never logged";
    EXPECT_TRUE(MenuShown(bar)) << "the failure showed no menu row";
}
