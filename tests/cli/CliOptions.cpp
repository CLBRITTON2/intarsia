#include "cli/CliOptions.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

    const std::filesystem::path kDefaultConfig = L"C:\\default\\config.lua";

    CliOptions                  Parse(const std::vector<std::wstring>& arguments) {
        return ParseCliOptions(arguments, kDefaultConfig);
    }

} // namespace

TEST(CliOptions, MonitorsKeepsTheDefaultConfig) {
    const CliOptions options = Parse({L"monitors"});
    EXPECT_EQ(options.command, CliCommand::Monitors);
    EXPECT_EQ(options.configPath, kDefaultConfig);
}

TEST(CliOptions, MonitorsReadsTheConfigPath) {
    const CliOptions options = Parse({L"monitors", L"--config", L"D:\\scratch\\config.lua"});
    EXPECT_EQ(options.configPath, std::filesystem::path(L"D:\\scratch\\config.lua"));
}

TEST(CliOptions, CheckReadsTheConfigPath) {
    EXPECT_EQ(Parse({L"check"}).command, CliCommand::Check);
    const CliOptions options = Parse({L"check", L"--config", L"D:\\scratch\\config.lua"});
    EXPECT_EQ(options.command, CliCommand::Check);
    EXPECT_EQ(options.configPath, std::filesystem::path(L"D:\\scratch\\config.lua"));
}

TEST(CliOptions, RejectsBadArguments) {
    EXPECT_THROW(Parse({}), CliOptionsError);
    EXPECT_THROW(Parse({L"--config", L"D:\\scratch\\config.lua"}), CliOptionsError);
    EXPECT_THROW(Parse({L"workspaces"}), CliOptionsError);
    EXPECT_THROW(Parse({L"monitors", L"--config"}), CliOptionsError);
    EXPECT_THROW(Parse({L"monitors", L"--log", L"D:\\scratch\\bar.log"}), CliOptionsError);
    EXPECT_THROW(Parse({L"check", L"--config"}), CliOptionsError);
}
