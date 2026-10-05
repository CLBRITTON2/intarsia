#include "LaunchOptions.hpp"
#include "MonitorTable.hpp"
#include "cli/CliOptions.hpp"
#include "config/Config.hpp"
#include "platform/Console.hpp"
#include "platform/Monitors.hpp"
#include "platform/Process.hpp"

#include <windows.h>

#include <exception>
#include <format>
#include <string>

// A console program beside the GUI bar, since a shell waits only for a console program and prints its output in order.
int wmain() { // NOLINT(misc-use-internal-linkage)
    try {
        RestrictDllSearchToSystem32();
        const CliOptions options = ParseCliOptions(CommandLineArguments(), DefaultConfigPath());
        const Config     config  = LoadConfig(options.configPath);
        WriteUtf8(GetStdHandle(STD_OUTPUT_HANDLE), MonitorTable(DescribeMonitors(ListMonitors()), config.bar));
        return 0;
    } catch (const CliOptionsError& error) {
        WriteUtf8(GetStdHandle(STD_ERROR_HANDLE), std::format("{}\n{}\n", error.what(), kCliUsage));
        return 2;
    } catch (const std::exception& error) {
        WriteUtf8(GetStdHandle(STD_ERROR_HANDLE), std::format("{}\n", error.what()));
        return 1;
    }
}
