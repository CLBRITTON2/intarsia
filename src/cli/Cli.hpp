#pragma once

#include "config/Config.hpp"
#include "platform/Monitors.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

enum class CliCommand : std::uint8_t {
    Check,   // load the config and its style as the bar would, and report the first error
    Monitors // print each monitor's id and whether the config gives it a bar
};

struct CliOptions {
    CliCommand command;
    std::filesystem::path configPath;
};

class CliOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws CliOptionsError naming the bad argument.
CliOptions ParseCliOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath);

// The intarsia-cli monitors listing: one row per target with its id, name, connection, whether its monitor is primary and whether
// bar gives that monitor a bar, in columns under a header. Each unsafe character of a name prints as U+FFFD.
std::string MonitorTable(const std::vector<MonitorDescription>& descriptions, const BarConfig& bar);
