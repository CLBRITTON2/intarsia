#include "platform/Launcher.hpp"

#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <shellapi.h>

#include <cstddef>
#include <string>

namespace {

    constexpr size_t kMaxQueuedLaunches = 8;

    // ShellExecuteExW can use COM shell extensions, which need an apartment on the calling thread. Nothing here may throw
    // out of the launcher's thread, so a failed launch is logged.
    void Start(const Command& command) {
        const std::wstring program    = command.program.wstring();
        const std::wstring parameters = JoinArguments(command.arguments);
        try {
            const ComScope    com(ComThreading::Single, "launcher");
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            // Zone checks stay on, so a program downloaded from the internet still gets its warning.
            info.fMask  = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpVerb = L"open";
            info.lpFile = program.c_str();
            if (!parameters.empty()) {
                info.lpParameters = parameters.c_str();
            }
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) {
                ThrowLastError("ShellExecuteExW");
            }
        } catch (const Win32Error& error) {
            Log(LogLevel::Warning, "launcher", "launch_failed",
                {{.key = "reason", .value = error.what()}, {.key = "program", .value = ToUtf8(program)}, {.key = "arguments", .value = ToUtf8(parameters)}});
        }
    }

} // namespace

Launcher::Launcher() : _queue(kMaxQueuedLaunches, [] { return WorkQueue<Command>::Process(Start); }) {}

void Launcher::Launch(const Command& command) {
    if (!_queue.Push(command)) {
        Log(LogLevel::Warning, "launcher", "launch_dropped", {{.key = "reason", .value = "queue full"}, {.key = "program", .value = ToUtf8(command.program.wstring())}});
    }
}
