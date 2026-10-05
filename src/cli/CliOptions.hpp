#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

constexpr std::string_view kCliUsage = "usage: intarsia-cli monitors [--config <path>]";

enum class CliCommand : std::uint8_t {
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
