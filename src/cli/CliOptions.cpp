#include "cli/CliOptions.hpp"

#include "platform/Text.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace {

    CliCommand ParseCommand(const std::wstring& name) {
        if (name == L"check") {
            return CliCommand::Check;
        }
        if (name == L"monitors") {
            return CliCommand::Monitors;
        }
        throw CliOptionsError(std::format("unknown command '{}', intarsia-cli takes check or monitors", ToUtf8(name)));
    }

} // namespace

CliOptions ParseCliOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath) {
    if (arguments.empty()) {
        throw CliOptionsError("intarsia-cli needs a command");
    }
    CliOptions options{.command = ParseCommand(arguments[0]), .configPath = defaultConfigPath};
    for (size_t index = 1; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (name != L"--config") {
            throw CliOptionsError(std::format("unknown option '{}', {} takes --config <path>", ToUtf8(name), ToUtf8(arguments[0])));
        }
        if (index + 1 >= arguments.size()) {
            throw CliOptionsError("--config needs a value");
        }
        options.configPath = arguments[index + 1];
    }
    return options;
}
