#include "LaunchOptions.hpp"
#include "MonitorTable.hpp"
#include "cli/CliOptions.hpp"
#include "config/Config.hpp"
#include "platform/Console.hpp"
#include "platform/Monitors.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <exception>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

    constexpr std::string_view kCliUsage = "usage: intarsia-cli check [--config <path>]\n"
                                           "       intarsia-cli monitors [--config <path>]";

    std::string                Check(const std::filesystem::path& configPath) {
        LoadConfig(configPath);
        return std::format("{} and {} load\n", ToUtf8(configPath.wstring()), ToUtf8(StylePath(configPath).wstring()));
    }

    std::string Monitors(const std::filesystem::path& configPath) {
        return MonitorTable(DescribeMonitors(ListMonitors()), LoadConfig(configPath).bar);
    }

    std::string Run(const CliOptions& options) {
        switch (options.command) {
            case CliCommand::Check: return Check(options.configPath);
            case CliCommand::Monitors: return Monitors(options.configPath);
        }
        throw std::logic_error("unhandled CliCommand");
    }

} // namespace

// A console program beside the GUI bar, since a shell waits only for a console program and prints its output in order.
int wmain() { // NOLINT(misc-use-internal-linkage)
    try {
        RestrictDllSearchToSystem32();
        WriteUtf8(GetStdHandle(STD_OUTPUT_HANDLE), Run(ParseCliOptions(CommandLineArguments(), DefaultConfigPath())));
        return 0;
    } catch (const CliOptionsError& error) {
        WriteUtf8(GetStdHandle(STD_ERROR_HANDLE), std::format("{}\n{}\n", error.what(), kCliUsage));
        return 2;
    } catch (const std::exception& error) {
        WriteUtf8(GetStdHandle(STD_ERROR_HANDLE), std::format("{}\n", error.what()));
        return 1;
    }
}
