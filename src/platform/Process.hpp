#pragma once

#include "platform/Errors.hpp"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// Limits DLL loading for the rest of the process to System32, so no DLL beside the executable or in the current
// directory can be planted into it. Call first thing at startup. The executable's own imports load before any code
// runs, so the link sets /DEPENDENTLOADFLAG for those.
void RestrictDllSearchToSystem32();

// The executable a process opened with PROCESS_QUERY_LIMITED_INFORMATION runs. Throws Win32Error.
std::filesystem::path ProcessImage(HANDLE process);

// The buffer GetTokenInformation fills for one class of a token opened with TOKEN_QUERY. Throws Win32Error naming name.
std::vector<std::byte> TokenBuffer(HANDLE token, TOKEN_INFORMATION_CLASS kind, std::string_view name);

// The user's SID inside a TokenUser buffer, valid while that buffer lives.
PSID UserSid(std::vector<std::byte>& tokenUser);

// The mandatory level's RID, such as SECURITY_MANDATORY_MEDIUM_RID. Throws Win32Error.
std::uint32_t IntegrityLevel(HANDLE token);

// A program to start with its arguments, never through a shell: the arguments reach the program as they are written.
struct Command {
    std::filesystem::path     program; // absolute, resolved when the config loads
    std::vector<std::wstring> arguments;

    bool                      operator==(const Command&) const = default;
};

// The absolute path of a program: as given when it is already absolute, else a bare file name's first match in the
// PATH directories (".exe" added when the name has no extension). Never the current directory, where a planted program
// would win, so a relative path with a directory part resolves to nothing. Nullopt when there is no such file.
std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program);

// Arguments joined into one command line that CommandLineToArgvW and the C runtime split back into the same arguments.
std::wstring JoinArguments(const std::vector<std::wstring>& arguments);

// This process's arguments, without the program name.
std::vector<std::wstring> CommandLineArguments();

// A command that started but gave no usable output: it timed out, was cancelled, printed too much or exited nonzero.
// The message is short enough to show on the bar.
class CommandError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Runs the command with no window and no shell, blocking until it ends, and returns what it printed to stdout. The
// command and every process it starts run in a job that is killed when the timeout passes, when cancel is signaled or
// when this returns, so nothing it starts outlives the call, and a process outside the job that holds its stdout holds
// the call no longer than that. It inherits only its stdout pipe: stdin and stderr are closed.
std::string CaptureOutput(const Command& command, std::chrono::milliseconds timeout, size_t maxBytes, HANDLE cancel);

// Runs the command as CaptureOutput does but with no timeout, calling onLine on this thread with each line it prints
// (without "\n" or "\r\n") as the line completes, and returns its exit code. Throws CommandError when cancel is
// signaled or a line passes maxLineBytes, and the job dies either way.
std::uint32_t StreamLines(const Command& command, size_t maxLineBytes, HANDLE cancel, const std::function<void(std::string_view)>& onLine);

// Far more than one line on a bar or one menu needs. Output past it fails the run instead of growing without bound.
constexpr size_t kMaxCommandOutputBytes = size_t{64} * 1024;

// Why a command gave no output, for callers on a worker thread that report a failure instead of throwing it.
struct RunFailure {
    std::string reason; // short enough to show on the bar
    std::string detail; // the full failure, for the log
};

// What run returns, or the RunFailure for the CommandError or Win32Error it throws.
template <typename Run>
std::variant<std::invoke_result_t<Run>, RunFailure> RunOrFailure(const Run& run) {
    try {
        return run();
    } catch (const CommandError& error) { return RunFailure{.reason = error.what(), .detail = error.what()}; } catch (const Win32Error& error) {
        return RunFailure{.reason = "could not run", .detail = error.what()};
    }
}

// Output that fails its run. The message never quotes the output, so it is always safe to draw and log.
class OutputError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Throws OutputError naming field when text is not UTF-8 or holds an unsafe character other than those in allowed.
void RequireDrawable(std::string_view text, std::string_view field, std::wstring_view allowed);
