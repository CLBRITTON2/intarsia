#include "Harness.hpp"

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
                           name, integration::LuaPath(integration::ArgvRecorderPath()), integration::LuaPath(record), printed, output, classes);
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

    // Prints class one, then class two about two seconds later, then exits with code 3 two seconds after that.
    constexpr std::string_view kContinuousScript = "@echo off\r\n"
                                                   "echo {\"text\":\"WWWW\",\"class\":\"one\"}\r\n"
                                                   "ping -n 3 127.0.0.1 >nul\r\n"
                                                   "echo {\"text\":\"WWWW\",\"class\":\"two\"}\r\n"
                                                   "ping -n 3 127.0.0.1 >nul\r\n"
                                                   "exit /b 3\r\n";

    // Large text, so glyph stems cover whole pixels in the exact color.
    std::string StyleWithRules(std::string_view rules) {
        return std::string(R"(
bar:
  font: Segoe UI
  size: 24
  color: #e0def4
section:
  margin: 4
  padding: 3
  background: #191724
module:
  alert:
    color: #e74c3c
)") + std::string(rules);
    }

} // namespace

TEST(Custom, DrawsTheColorOfTheClassItsJsonNames) {
    constexpr std::string_view    kTestName = "custom-class";
    const integration::RunningBar bar(kTestName, CustomConfig(R"("probe")", RecorderModule(kTestName, "probe", "json", R"("good")", R"([[{"text":"WWWW","class":"good"}]])")),
                                      StyleWithRules("probe:\n  color: #27ae60\n  good:\n    color: #f39c12\n"));
    EXPECT_TRUE(bar.Draws(kOrange, kDrawn)) << "the class the output names never drew its color";
    EXPECT_EQ(bar.PixelCount(kGreen), size_t{0}) << "the module's own color drew beside its class's";
}

TEST(Custom, DrawsTheAlertColorWhenItsCommandFails) {
    const integration::RunningBar bar(
        "custom-fails", CustomConfig(R"("probe")", R"(probe = { type = "custom", command = { "cmd", "/c", "exit 3" }, run = "interval", timeout = 5, output = "text", classes = {},
                  format = "{text}", icon = "", interval = 30 },)"),
        StyleWithRules(""));
    EXPECT_TRUE(bar.Draws(kRed, kDrawn)) << "the failing command never drew the alert color";
    EXPECT_FALSE(bar.Events("command_failed").empty()) << "the bar logged no command_failed";
}

TEST(Custom, ShowsEachLineAContinuousCommandPrintsThenItsExitThenItsRestart) {
    // Apart from the bar's work directory, which the bar's start empties.
    const std::filesystem::path script = integration::NewWorkDirectory("custom-continuous-script") / "lines.cmd";
    integration::WriteText(script, kContinuousScript);
    const std::string module = std::format(R"(probe = {{ type = "custom", command = {{ {}, "/c", {} }}, run = "continuous", output = "json", classes = {{ "one", "two" }},
                  format = "{{text}}", icon = "", interval = 2 }},)",
                                           integration::LuaString(R"(C:\Windows\System32\cmd.exe)"), integration::LuaPath(script));
    const integration::RunningBar bar("custom-continuous", CustomConfig(R"("probe")", module), StyleWithRules("probe:\n  one:\n    color: #27ae60\n  two:\n    color: #f39c12\n"));

    ASSERT_TRUE(bar.Draws(kGreen, kDrawn)) << "the first line's class never drew";
    ASSERT_TRUE(bar.Draws(kOrange, kDrawn)) << "the second line's class never drew";
    EXPECT_EQ(bar.PixelCount(kGreen), size_t{0}) << "the first line's class still drew beside the second's";
    ASSERT_TRUE(bar.Draws(kRed, kDrawn)) << "the command's exit never drew the alert color";
    EXPECT_FALSE(bar.LogLines("exited with code 3").empty()) << "the bar logged no exit code";
    EXPECT_TRUE(bar.Draws(kGreen, kDrawn)) << "the command never restarted after its interval";
}
