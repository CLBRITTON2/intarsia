#pragma once

#include <windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// The command line: intarsia.exe [--config <path>] [--glazewm-port <port>].
struct LaunchOptions {
    std::filesystem::path configPath;
    USHORT                glazeWmPort; // the smoke harness points the bar at its own server while GlazeWM holds 6123
};

class LaunchOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws LaunchOptionsError naming the bad argument.
LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, USHORT defaultGlazeWmPort);

// This process's arguments, without the program name.
std::vector<std::wstring> CommandLineArguments();
