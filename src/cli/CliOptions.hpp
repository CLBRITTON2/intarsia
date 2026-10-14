#pragma once

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
    CliCommand            command;
    std::filesystem::path configPath;
};

class CliOptionsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// arguments excludes the program name. Throws CliOptionsError naming the bad argument.
CliOptions ParseCliOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath);
