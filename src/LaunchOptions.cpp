#include "LaunchOptions.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <shellapi.h>

#include <charconv>
#include <format>
#include <memory>

LaunchOptions ParseLaunchOptions(const std::vector<std::wstring>& arguments, const std::filesystem::path& defaultConfigPath, USHORT defaultGlazeWmPort) {
    LaunchOptions options{.configPath = defaultConfigPath, .glazeWmPort = defaultGlazeWmPort};
    for (size_t index = 0; index < arguments.size(); index += 2) {
        const std::wstring& name = arguments[index];
        if (index + 1 >= arguments.size()) {
            throw LaunchOptionsError(std::format("{} needs a value", ToUtf8(name)));
        }
        const std::string value = ToUtf8(arguments[index + 1]);
        if (name == L"--config") {
            options.configPath = arguments[index + 1];
        } else if (name == L"--glazewm-port") {
            unsigned int port       = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
            if (error != std::errc() || end != value.data() + value.size() || port == 0 || port > 65535) {
                throw LaunchOptionsError(std::format("--glazewm-port must be a port from 1 to 65535, got '{}'", value));
            }
            options.glazeWmPort = static_cast<USHORT>(port);
        } else {
            throw LaunchOptionsError(std::format("unknown option '{}', intarsia takes --config <path> and --glazewm-port <port>", ToUtf8(name)));
        }
    }
    return options;
}

std::vector<std::wstring> CommandLineArguments() {
    int     count = 0;
    LPWSTR* raw   = CommandLineToArgvW(GetCommandLineW(), &count);
    if (raw == nullptr) {
        ThrowLastError("CommandLineToArgvW");
    }
    const std::unique_ptr<LPWSTR, decltype(&LocalFree)> owned(raw, &LocalFree);
    std::vector<std::wstring>                           arguments;
    for (int index = 1; index < count; ++index) {
        arguments.emplace_back(raw[index]);
    }
    return arguments;
}
