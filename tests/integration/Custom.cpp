#include "Harness.hpp"
#include "platform/Text.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace {

    constexpr std::uint32_t kGreen  = 0x27AE60;
    constexpr std::uint32_t kOrange = 0xF39C12;
    constexpr std::uint32_t kRed    = 0xE74C3C;
    constexpr auto          kDrawn  = std::chrono::seconds(5);

    // A custom module named name whose command is the recorder printing printed, recording to the test's work
    // directory. printed is a Lua expression.
    std::string RecorderModule(std::string_view testName, std::string_view name, std::string_view output, std::string_view classes, std::string_view printed) {
        const std::filesystem::path record = integration::WorkDirectoryPath(testName) / std::format("{}.txt", name);
        return std::format(R"({} = {{ type = "custom", command = {{ {}, {}, {} }}, run = "interval", timeout = 5, output = "{}", classes = {{ {} }}, format = "{{text}}", icon = "",
                  interval = 30 }},)",
                           name, integration::LuaString(ToUtf8(integration::ArgvRecorderPath().wstring())), integration::LuaString(ToUtf8(record.wstring())), printed, output,
                           classes);
    }

    // left is the Lua list contents of the left section, modules the Lua definitions of the modules it names.
    std::string CustomConfig(std::string_view left, std::string_view modules) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40 }},
    window_manager = "glazewm",
    modules_left = {{ {} }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{
        {}
    }},
}}
)",
                           left, modules);
    }

    // Large text, so glyph stems cover whole pixels in the exact color.
    std::string StyleWithRules(std::string_view rules) {
        return std::string(R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
.alert { color: #e74c3c; }
)") + std::string(rules);
    }

} // namespace

TEST(Custom, DrawsTheColorOfTheClassItsJsonNames) {
    constexpr std::string_view    kTestName = "custom-class";
    const integration::RunningBar bar(kTestName, CustomConfig(R"("probe")", RecorderModule(kTestName, "probe", "json", R"("good")", R"([[{"text":"WWWW","class":"good"}]])")),
                                      StyleWithRules("#probe { color: #27ae60; } #probe.good { color: #f39c12; }"));
    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the class the output names never drew its color";
    EXPECT_EQ(bar.PixelsOf(kGreen), size_t{0}) << "the module's own color drew beside its class's";
}

TEST(Custom, DrawsTheAlertColorWhenItsCommandFails) {
    const integration::RunningBar bar(
        "custom-fails", CustomConfig(R"("probe")", R"(probe = { type = "custom", command = { "cmd", "/c", "exit 3" }, run = "interval", timeout = 5, output = "text", classes = {},
                  format = "{text}", icon = "", interval = 30 },)"),
        StyleWithRules(""));
    EXPECT_TRUE(bar.Draws(kRed, kDrawn)) << "the failing command never drew the alert color";
    EXPECT_FALSE(bar.LogLines("event=command_failed").empty()) << "the bar logged no command_failed";
}
