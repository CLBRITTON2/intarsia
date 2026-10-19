#include "cli/Cli.hpp"
#include "config/Config.hpp"
#include "platform/Errors.hpp"
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

    std::string Check(const std::filesystem::path& configPath) {
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

    bool IsConsole(HANDLE output) {
        DWORD mode = 0;
        return GetConsoleMode(output, &mode) != 0;
    }

    void WriteBytes(HANDLE output, std::string_view utf8) {
        while (!utf8.empty()) {
            DWORD written = 0;
            if (!WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
                ThrowLastError("WriteFile");
            }
            utf8.remove_prefix(written);
        }
    }

    void WriteToConsole(HANDLE console, std::string_view utf8) {
        const std::wstring wide = ToWide(utf8);
        std::wstring_view text = wide;
        while (!text.empty()) {
            DWORD written = 0;
            if (!WriteConsoleW(console, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
                ThrowLastError("WriteConsoleW");
            }
            text.remove_prefix(written);
        }
    }

    // Writes UTF-8 text to a standard handle: as UTF-16 to a console, whose code page need not be UTF-8, and as the bytes
    // themselves to a pipe or file. Throws Win32Error when a write fails.
    void WriteUtf8(HANDLE output, std::string_view utf8) {
        if (IsConsole(output)) {
            WriteToConsole(output, utf8);
            return;
        }
        WriteBytes(output, utf8);
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
