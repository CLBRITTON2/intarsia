#include "App.hpp"
#include "LaunchOptions.hpp"
#include "config/Config.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "tray/ShellNotifyIcon.hpp"

#include <windows.h>

#include <cstdint>
#include <exception>

namespace {

    constexpr std::uint16_t kGlazeWmPort = 6123;

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        RestrictDllSearchToSystem32();
        App app(instance, ParseLaunchOptions(CommandLineArguments(), DefaultConfigPath(), kGlazeWmPort, kTaskbarClass));
        return app.Run();
    } catch (const std::exception& error) {
        // stderr reaches a launching shell that redirected it, the message box covers a plain double-click.
        Log(LogLevel::Error, "app", "fatal", {{.key = "reason", .value = error.what()}});
        MessageBoxA(nullptr, error.what(), "intarsia", MB_ICONERROR | MB_OK);
        return 1;
    }
}
