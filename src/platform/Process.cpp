#include "platform/Process.hpp"

#include "platform/Text.hpp"

#include <objbase.h>
#include <shellapi.h>

#include <array>
#include <cstdio>

namespace {

    constexpr size_t kMaxQueuedLaunches = 8;
    constexpr size_t kLogExcerptBytes   = 300;

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

    void LogLaunchFailure(const Command& command, DWORD error) {
        std::fprintf(stderr, "intarsia: warning component=launcher event=launch_failed error=%lu program=\"%s\" arguments=\"%s\"\n", error,
                     LogExcerpt(ToUtf8(command.program.wstring()), kLogExcerptBytes).c_str(), LogExcerpt(ToUtf8(JoinArguments(command.arguments)), kLogExcerptBytes).c_str());
    }

} // namespace

std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program) {
    const std::filesystem::path path(program);
    if (path.is_absolute()) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? std::optional<std::filesystem::path>(path) : std::nullopt;
    }
    std::array<wchar_t, 32768> found{};
    const DWORD                length = SearchPathW(nullptr, std::wstring(program).c_str(), L".exe", static_cast<DWORD>(found.size()), found.data(), nullptr);
    if (length == 0 || length >= found.size()) {
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

Launcher::Launcher() :
    stopEvent_(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(launcher stop)")),
    queuedEvent_(OwnHandle(CreateEventW(nullptr, FALSE, FALSE, nullptr), "CreateEventW(launcher queued)")), thread_([this] { Run(); }) {}

Launcher::~Launcher() {
    SetEvent(stopEvent_.get());
    thread_.join();
}

void Launcher::Launch(const Command& command) {
    {
        const std::scoped_lock lock(mutex_);
        if (queue_.size() >= kMaxQueuedLaunches) {
            std::fprintf(stderr, "intarsia: warning component=launcher event=launch_dropped reason=\"queue full\" program=\"%s\"\n",
                         LogExcerpt(ToUtf8(command.program.wstring()), kLogExcerptBytes).c_str());
            return;
        }
        queue_.push_back(command);
    }
    SetEvent(queuedEvent_.get());
}

// ShellExecuteExW can use COM shell extensions, which need an apartment on the calling thread.
void Launcher::Run() {
    const HRESULT               initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const std::array<HANDLE, 2> waits{stopEvent_.get(), queuedEvent_.get()};
    while (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        std::deque<Command> commands;
        {
            const std::scoped_lock lock(mutex_);
            commands.swap(queue_);
        }
        for (const Command& command : commands) {
            const std::wstring program    = command.program.wstring();
            const std::wstring parameters = JoinArguments(command.arguments);
            SHELLEXECUTEINFOW  info{};
            info.cbSize = sizeof(info);
            // No zone checks or file association dialogs: the program is an absolute path from the user's own config.
            info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS;
            info.lpVerb       = L"open";
            info.lpFile       = program.c_str();
            info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
            info.nShow        = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) {
                LogLaunchFailure(command, GetLastError());
            }
        }
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
}
