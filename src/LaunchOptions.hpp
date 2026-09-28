#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// The command line: intarsia.exe [--config <path>] [--glazewm-port <port>] [--tray-class <name>].
struct LaunchOptions {
    std::filesystem::path configPath;
    std::uint16_t         glazeWmPort; // the stress tests point the bar at its own server while GlazeWM holds 6123
    // The stress tests give the tray a class no app sends to, so it never takes the desktop's icons.
    std::wstring trayClass;
};

class LaunchOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws LaunchOptionsError naming the bad argument.
LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, std::uint16_t defaultGlazeWmPort,
                                 const std::wstring& defaultTrayClass);

// This process's arguments, without the program name.
std::vector<std::wstring> CommandLineArguments();
