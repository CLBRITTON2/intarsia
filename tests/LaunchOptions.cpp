#include "LaunchOptions.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    const std::filesystem::path kDefaultConfig = L"C:\\default\\config.lua";
    const std::filesystem::path kDefaultLog    = L"C:\\default\\intarsia.log";

    LaunchOptions               Parse(const std::vector<std::wstring>& arguments) {
        return ParseLaunchOptions(arguments, kDefaultConfig, kDefaultLog, 6123, L"DefaultTray");
    }

} // namespace

TEST(LaunchOptions, NoArgumentsKeepTheDefaults) {
    const LaunchOptions options = Parse({});
    EXPECT_EQ(options.configPath, kDefaultConfig);
    EXPECT_EQ(options.logPath, kDefaultLog);
    EXPECT_EQ(options.glazeWmPort, 6123);
    EXPECT_EQ(options.trayClass, L"DefaultTray");
}

TEST(LaunchOptions, ReadsTheConfigPathLogPathPortAndTrayClass) {
    const LaunchOptions options = Parse({L"--config", L"D:\\soak\\config.lua", L"--log", L"D:\\soak\\bar.log", L"--glazewm-port", L"50123", L"--tray-class", L"SoakTray"});
    EXPECT_EQ(options.configPath, std::filesystem::path(L"D:\\soak\\config.lua"));
    EXPECT_EQ(options.logPath, std::filesystem::path(L"D:\\soak\\bar.log"));
    EXPECT_EQ(options.glazeWmPort, 50123);
    EXPECT_EQ(options.trayClass, L"SoakTray");
}

TEST(LaunchOptions, RejectsBadArguments) {
    EXPECT_THROW(Parse({L"--config"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--log"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"0"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"70000"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--glazewm-port", L"12ab"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--verbose", L"yes"}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--tray-class", L""}), LaunchOptionsError);
    EXPECT_THROW(Parse({L"--tray-class", std::wstring(256, L'x')}), LaunchOptionsError);
}
