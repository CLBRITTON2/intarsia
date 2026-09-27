#include "LaunchOptions.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    const std::filesystem::path kDefaultConfig = L"C:\\default\\config.lua";

    LaunchOptions               Parse(const std::vector<std::wstring>& arguments) {
        return ParseLaunchOptions(arguments, kDefaultConfig, 6123);
    }

} // namespace

TEST(LaunchOptions, NoArgumentsKeepTheDefaults) {
    const LaunchOptions options = Parse({});
    EXPECT_EQ(options.configPath, kDefaultConfig);
    EXPECT_EQ(options.glazeWmPort, 6123);
}

TEST(LaunchOptions, ReadsTheConfigPathAndPort) {
    const LaunchOptions options = Parse({L"--config", L"D:\\stress\\config.lua", L"--glazewm-port", L"50123"});
    EXPECT_EQ(options.configPath, std::filesystem::path(L"D:\\stress\\config.lua"));
    EXPECT_EQ(options.glazeWmPort, 50123);
}

TEST(LaunchOptions, RejectsBadArguments) {
    EXPECT_THROW(Parse({L"--config"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"0"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"70000"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"12ab"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--verbose", L"yes"}), LaunchOptionsError);
}
