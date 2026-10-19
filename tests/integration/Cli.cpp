#include "Harness.hpp"

#include "ChildProcess.hpp"
#include "platform/Text.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using testing::Contains;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;

namespace {

    constexpr std::string_view kStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
)";

    constexpr auto kExitLimit = std::chrono::seconds(10);

    std::string ConfigWithMonitors(std::string_view monitors) {
        return std::string(R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = )") +
            std::string(monitors) + R"( },
    window_manager = "windows",
    modules_left = { "time" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { time = { type = "clock", format = "{time:%H:%M}", icon = "", interval = 60 } },
}
)";
    }

    struct CliRun {
        std::uint32_t exitCode;
        std::vector<std::string> output;
        std::vector<std::string> errors;
    };

    CliRun RunCli(std::string_view testName, std::wstring_view command, std::string_view config) {
        const std::filesystem::path directory = integration::NewWorkDirectory(testName);
        const std::filesystem::path configPath = directory / "config.lua";
        integration::WriteText(configPath, config);
        integration::WriteText(directory / "style.conf", kStyle);
        const integration::ChildProcess cli(integration::CliPath(), {std::wstring(command), L"--config", configPath.wstring()}, directory / "stdout.txt", directory / "stderr.txt");
        const std::optional<std::uint32_t> exitCode = cli.Close(kExitLimit);
        if (!exitCode.has_value()) {
            throw std::runtime_error(std::format("intarsia-cli {} did not exit within {} s ({})", ToUtf8(command), kExitLimit.count(), directory.string()));
        }
        return CliRun{
            .exitCode = *exitCode,
            .output = integration::ReadLines(directory / "stdout.txt"),
            .errors = integration::ReadLines(directory / "stderr.txt"),
        };
    }

} // namespace

TEST(Cli, CheckPrintsThatAValidConfigLoads) {
    const CliRun run = RunCli("cli_check_valid", L"check", ConfigWithMonitors(R"({ "*" })"));
    EXPECT_EQ(run.exitCode, 0U);
    EXPECT_THAT(run.output, ElementsAre(HasSubstr("load")));
    EXPECT_THAT(run.errors, IsEmpty());
}

TEST(Cli, CheckPrintsTheFirstErrorAndExitsOne) {
    const CliRun run = RunCli("cli_check_invalid", L"check", ConfigWithMonitors(R"({ "primary", "primary" })"));
    EXPECT_EQ(run.exitCode, 1U);
    EXPECT_THAT(run.output, IsEmpty());
    EXPECT_THAT(run.errors, Contains(HasSubstr("bar.monitors lists 'primary' twice")));
}

TEST(Cli, PrintsUsageAndExitsTwoOnAnUnknownCommand) {
    const CliRun run = RunCli("cli_unknown_command", L"frobnicate", ConfigWithMonitors(R"({ "*" })"));
    EXPECT_EQ(run.exitCode, 2U);
    EXPECT_THAT(run.output, IsEmpty());
    EXPECT_THAT(run.errors, Contains(HasSubstr("usage: intarsia-cli")));
}
