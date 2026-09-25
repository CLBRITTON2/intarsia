#include "platform/Process.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

#include <objbase.h>
#include <shellapi.h>

#include <array>
#include <format>

namespace {

    constexpr size_t kMaxQueuedLaunches = 8;

    bool             NeedsQuotes(std::wstring_view argument) {
        return argument.empty() || argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    }

    // The CommandLineToArgvW rules: backslashes are literal unless they precede a quote, where each one needs doubling
    // and the quote itself needs a backslash.
    std::wstring QuoteArgument(std::wstring_view argument) {
        if (!NeedsQuotes(argument)) {
            return std::wstring(argument);
        }
        std::wstring quoted      = L"\"";
        size_t       backslashes = 0;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashes;
                continue;
            }
            if (character == L'"') {
                quoted.append((backslashes * 2) + 1, L'\\');
            } else {
                quoted.append(backslashes, L'\\');
            }
            backslashes = 0;
            quoted += character;
        }
        quoted.append(backslashes * 2, L'\\');
        quoted += L'"';
        return quoted;
    }

    std::wstring PathVariable() {
        const DWORD length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (length == 0) {
            return {};
        }
        std::wstring value(length, L'\0');
        const DWORD  copied = GetEnvironmentVariableW(L"PATH", value.data(), length);
        value.resize(copied);
        return value;
    }

    // ShellExecuteExW can use COM shell extensions, which need an apartment on the calling thread.
    void Start(const Command& command) {
        const HRESULT      initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const std::wstring program     = command.program.wstring();
        const std::wstring parameters  = JoinArguments(command.arguments);
        SHELLEXECUTEINFOW  info{};
        info.cbSize = sizeof(info);
        // Zone checks stay on, so a program downloaded from the internet still gets its warning.
        info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb       = L"open";
        info.lpFile       = program.c_str();
        info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
        info.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) {
            Log(LogLevel::Warning, "launcher", "launch_failed",
                {{.key = "error", .value = std::to_string(GetLastError())}, {.key = "program", .value = ToUtf8(program)}, {.key = "arguments", .value = ToUtf8(parameters)}});
        }
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
    }

} // namespace

std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program) {
    const std::filesystem::path path(program);
    if (path.is_absolute()) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? std::optional<std::filesystem::path>(path) : std::nullopt;
    }
    // An explicit search path keeps SearchPathW from also looking in the current directory.
    const std::wstring         searchPath = PathVariable();
    std::array<wchar_t, 32768> found{};
    const DWORD                length = SearchPathW(searchPath.c_str(), std::wstring(program).c_str(), L".exe", static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (searchPath.empty() || length == 0 || length >= found.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(std::wstring(found.data(), length));
}

std::wstring JoinArguments(const std::vector<std::wstring>& arguments) {
    std::wstring line;
    for (const std::wstring& argument : arguments) {
        if (!line.empty()) {
            line += L' ';
        }
        line += QuoteArgument(argument);
    }
    return line;
}

void RestrictDllSearchToSystem32() {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        ThrowLastError("SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)");
    }
}

Launcher::Launcher() : _queue(kMaxQueuedLaunches, Start) {}

void Launcher::Launch(const Command& command) {
    if (!_queue.Push(command)) {
        Log(LogLevel::Warning, "launcher", "launch_dropped", {{.key = "reason", .value = "queue full"}, {.key = "program", .value = ToUtf8(command.program.wstring())}});
    }
}
