#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// The command line: intarsia.exe [--config <path>] [--log <path>] [--glazewm-port <port>] [--tray-class <name>].
struct LaunchOptions {
    std::filesystem::path configPath;
    // The soak tests log beside their config, since a running bar keeps the default log open.
    std::filesystem::path logPath;
    std::uint16_t         glazeWmPort; // the soak tests point the bar at its own server while GlazeWM holds 6123
    // The soak tests give the tray a class no app sends to, so it never takes the desktop's icons.
    std::wstring trayClass;
};

class LaunchOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws LaunchOptionsError naming the bad argument.
LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, const std::filesystem::path& defaultLogPath,
                                 std::uint16_t defaultGlazeWmPort, const std::wstring& defaultTrayClass);

// This process's arguments, without the program name.
std::vector<std::wstring> CommandLineArguments();
