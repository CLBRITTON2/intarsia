#include "Harness.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace {

    constexpr std::uint32_t kGreen = 0x27AE60;
    constexpr std::uint32_t kOrange = 0xF39C12;
    constexpr std::uint32_t kRed = 0xE74C3C;
    constexpr std::uint32_t kBlue = 0x2E86C1;

    std::string CustomConfig(std::string_view left, std::string_view modules) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40, monitors = {{ "*" }} }},
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

    // ping -n 3 is a two second sleep.
    constexpr std::string_view kContinuousScript = "@echo off\r\n"
                                                   "echo {\"text\":\"WWWW\",\"class\":\"one\"}\r\n"
                                                   "ping -n 3 127.0.0.1 >nul\r\n"
                                                   "echo {\"text\":\"WWWW\",\"class\":\"two\"}\r\n"
                                                   "ping -n 3 127.0.0.1 >nul\r\n"
                                                   "exit /b 3\r\n";

    // Large text, so glyph stems cover whole pixels in the exact color.
    std::string StyleWithRules(std::string_view rules) {
        return std::string(R"(
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
module {
  alert {
    color: #e74c3c
  }
}
)") + std::string(rules);
    }

} // namespace

TEST(Custom, DrawsTheAlertColorWhenItsCommandFails) {
    const integration::RunningBar bar(
        "custom-fails", CustomConfig(R"("probe")", R"(probe = { type = "custom", command = { "cmd", "/c", "exit 3" }, run = "interval", timeout = 5, output = "text", classes = {},
                  format = "{text}", icon = "", interval = 30 },)"),
        StyleWithRules(""));
    EXPECT_TRUE(bar.Draws(kRed, integration::kLimit)) << "the failing command never drew the alert color";
    EXPECT_FALSE(bar.Events("command_failed").empty()) << "the bar logged no command_failed";
}

TEST(Custom, ShowsEachLineAContinuousCommandPrintsInItsClassColorThenItsExitThenItsRestart) {
    // Apart from the bar's work directory, which the bar's start empties.
    const std::filesystem::path script = integration::NewWorkDirectory("custom-continuous-script") / "lines.cmd";
    integration::WriteText(script, kContinuousScript);
    const std::string module = std::format(R"(probe = {{ type = "custom", command = {{ {}, "/c", {} }}, run = "continuous", output = "json", classes = {{ "one", "two" }},
                  format = "{{text}}", icon = "", interval = 2 }},)",
                                           integration::LuaString(R"(C:\Windows\System32\cmd.exe)"), integration::LuaPath(script));
    const integration::RunningBar bar("custom-continuous", CustomConfig(R"("probe")", module),
                                      StyleWithRules("probe {\n  color: #2e86c1\n  one {\n    color: #27ae60\n  }\n  two {\n    color: #f39c12\n  }\n}\n"));

    ASSERT_TRUE(bar.Draws(kGreen, integration::kLimit)) << "the first line's class never drew";
    EXPECT_EQ(bar.PixelCount(kBlue), size_t{0}) << "the module's own color drew beside its class's";
    ASSERT_TRUE(bar.Draws(kOrange, integration::kLimit)) << "the second line's class never drew";
    EXPECT_EQ(bar.PixelCount(kGreen), size_t{0}) << "the first line's class still drew beside the second's";
    ASSERT_TRUE(bar.Draws(kRed, integration::kLimit)) << "the command's exit never drew the alert color";
    EXPECT_FALSE(bar.LogLines("exited with code 3").empty()) << "the bar logged no exit code";
    EXPECT_TRUE(bar.Draws(kGreen, integration::kLimit)) << "the command never restarted after its interval";
}
