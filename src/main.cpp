#include "App.hpp"
#include "LaunchOptions.hpp"
#include "MonitorTable.hpp"
#include "config/Config.hpp"
#include "platform/Console.hpp"
#include "platform/Log.hpp"
#include "platform/Monitors.hpp"
#include "platform/PeerProcess.hpp"
#include "platform/Process.hpp"
#include "platform/ShellLaunch.hpp"
#include "platform/Text.hpp"
#include "platform/Token.hpp"
#include "tray/ShellNotifyIcon.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace {

    constexpr std::uint16_t kGlazeWmPort = 6123;
    // Long enough for Explorer to start at logon alongside an elevated window manager that starts the bar.
    constexpr std::chrono::seconds kShellWait{30};

    // An elevated bar gets no unelevated app's tray messages (UIPI) and runs the config's commands elevated. The shell
    // is asked only then, so an unelevated bar never waits for one.
    bool RunsAboveShell() {
        const std::uint32_t level = IntegrityLevel(GetCurrentProcessToken());
        return level > SECURITY_MANDATORY_MEDIUM_RID && level > ShellIntegrityLevel(kShellWait);
    }

    int PrintMonitors(const std::filesystem::path& configPath) {
        try {
            const Config config = LoadConfig(configPath);
            WriteToCaller(MonitorTable(DescribeMonitors(ListMonitors()), config.bar));
            return 0;
        } catch (const ConfigError& error) {
            WriteToCaller(std::string(error.what()) + "\n");
            return 1;
        }
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        RestrictDllSearchToSystem32();
        LogCrashes();
        LaunchOptions options = ParseLaunchOptions(CommandLineArguments(), DefaultConfigPath(), DefaultLogPath(), kGlazeWmPort, kTaskbarClass);
        // Before the relaunch, which would lose the console the table prints to.
        if (options.listMonitors) {
            return PrintMonitors(options.configPath);
        }
        // Before the log opens, since the relaunched bar rotates it.
        if (RunsAboveShell()) {
            StartThroughShell(ProcessImage(GetCurrentProcess()), CommandLineArguments());
            Log(LogLevel::Info, "app", "relaunched_through_shell", {{.key = "process", .value = std::to_string(GetCurrentProcessId())}});
            return 0;
        }
        LogToFile(options.logPath);
        Log(LogLevel::Info, "app", "started", {{.key = "process", .value = std::to_string(GetCurrentProcessId())}, {.key = "log", .value = ToUtf8(options.logPath.wstring())}});
        App app(instance, std::move(options));
        return app.Run();
    } catch (const std::exception& error) {
        // The log file keeps it, stderr reaches a launching shell that redirected it, the message box covers a plain
        // double-click.
        Log(LogLevel::Error, "app", "fatal", {{.key = "reason", .value = error.what()}});
        MessageBoxA(nullptr, error.what(), "intarsia", MB_ICONERROR | MB_OK);
        return 1;
    }
}
