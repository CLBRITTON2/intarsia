#include "LaunchOptions.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <shellapi.h>

#include <cstdint>
#include <charconv>
#include <format>

namespace {

    // RegisterClassExW takes a class name of up to 256 characters with its null.
    constexpr size_t kMaxClassNameLength = 255;

} // namespace

LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, const std::filesystem::path& defaultLogPath,
                                 std::uint16_t defaultGlazeWmPort, const std::wstring& defaultTrayClass) {
    LaunchOptions options{
        .configPath  = defaultConfigPath,
        .logPath     = defaultLogPath,
        .glazeWmPort = defaultGlazeWmPort,
        .trayClass   = defaultTrayClass,
    };
    for (size_t index = 0; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (index + 1 >= arguments.size()) {
            throw LaunchOptionsError(std::format("{} needs a value", ToUtf8(name)));
        }
        const std::wstring& value = arguments[index + 1];
        if (name == L"--config") {
            options.configPath = value;
        } else if (name == L"--log") {
            options.logPath = value;
        } else if (name == L"--glazewm-port") {
            const std::string portText = ToUtf8(value);
            std::uint32_t     port     = 0;
            const auto [end, error]    = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            if (error != std::errc() || end != portText.data() + portText.size() || port == 0 || port > 65535) {
                throw LaunchOptionsError(std::format("--glazewm-port must be a port from 1 to 65535, got '{}'", portText));
            }
            options.glazeWmPort = static_cast<std::uint16_t>(port);
        } else if (name == L"--tray-class") {
            if (value.empty() || value.size() > kMaxClassNameLength) {
                throw LaunchOptionsError(std::format("--tray-class must be a window class name of 1 to {} characters, got {}", kMaxClassNameLength, value.size()));
            }
            options.trayClass = value;
        } else {
            throw LaunchOptionsError(std::format("unknown option '{}', intarsia takes --config <path>, --log <path>, --glazewm-port <port> and --tray-class <name>", ToUtf8(name)));
        }
    }
    return options;
}

std::vector<std::wstring> CommandLineArguments() {
    std::int32_t count = 0;
    LPWSTR*      raw   = CommandLineToArgvW(GetCommandLineW(), &count);
    if (raw == nullptr) {
        ThrowLastError("CommandLineToArgvW");
    }
    const UniqueReleased<HLOCAL, &LocalFree> owned(static_cast<HLOCAL>(raw));
    std::vector<std::wstring>                arguments;
    for (std::int32_t index = 1; index < count; ++index) {
        arguments.emplace_back(raw[index]);
    }
    return arguments;
}
