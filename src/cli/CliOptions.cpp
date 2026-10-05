#include "cli/CliOptions.hpp"

#include "platform/Text.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <vector>

CliOptions ParseCliOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath) {
    if (arguments.empty()) {
        throw CliOptionsError("intarsia-cli needs a command");
    }
    if (arguments[0] != L"monitors") {
        throw CliOptionsError(std::format("unknown command '{}', intarsia-cli takes monitors", ToUtf8(arguments[0])));
    }
    CliOptions options{.command = CliCommand::Monitors, .configPath = defaultConfigPath};
    for (size_t index = 1; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (name != L"--config") {
            throw CliOptionsError(std::format("unknown option '{}', monitors takes --config <path>", ToUtf8(name)));
        }
        if (index + 1 >= arguments.size()) {
            throw CliOptionsError("--config needs a value");
        }
        options.configPath = arguments[index + 1];
    }
    return options;
}
