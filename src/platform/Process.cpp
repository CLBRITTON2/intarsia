#include "platform/Process.hpp"

#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Token.hpp"

#include <sddl.h>
#include <shellapi.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    constexpr size_t        kMaxQueuedLaunches = 8;
    constexpr std::uint32_t kKilledExitCode    = 1;

    bool                    NeedsQuotes(std::wstring_view argument) {
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

    // PATH's absolute entries, joined for SearchPathW, which resolves a relative one (".", an empty one, "bin")
    // against the current directory.
    std::wstring AbsoluteEntries(std::wstring_view path) {
        std::wstring kept;
        for (const auto entry : std::views::split(path, L';')) {
            const std::wstring_view text(entry.begin(), entry.end());
            if (!std::filesystem::path(text).is_absolute()) {
                continue;
            }
            if (!kept.empty()) {
                kept += L';';
            }
            kept += text;
        }
        return kept;
    }

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
            info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpVerb       = L"open";
            info.lpFile       = program.c_str();
            info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
            info.nShow        = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) {
                ThrowLastError("ShellExecuteExW");
            }
        } catch (const Win32Error& error) {
            Log(LogLevel::Warning, "launcher", "launch_failed",
                {{.key = "reason", .value = error.what()}, {.key = "program", .value = ToUtf8(program)}, {.key = "arguments", .value = ToUtf8(parameters)}});
        }
    }

    enum class Stop : std::uint8_t {
        None,
        TimedOut,
        Cancelled
    };

    struct Watchdog {
        HANDLE            job;
        HANDLE            stopped; // manual reset, ends the read even while a process outside the job holds the pipe
        std::atomic<Stop> stop;
    };

    // Runs on a thread pool thread once cancel is signaled or the timeout passes, whichever comes first.
    void CALLBACK StopJob(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT, TP_WAIT_RESULT result) {
        auto* watchdog = static_cast<Watchdog*>(context);
        watchdog->stop.store(result == WAIT_TIMEOUT ? Stop::TimedOut : Stop::Cancelled);
        TerminateJobObject(watchdog->job, kKilledExitCode);
        SetEvent(watchdog->stopped);
    }

    // Disarms first and waits out a callback already running, so the Watchdog it points at is never used after this.
    struct ThreadpoolWaitCloser {
        void operator()(PTP_WAIT wait) const {
            SetThreadpoolWait(wait, nullptr, nullptr);
            WaitForThreadpoolWaitCallbacks(wait, TRUE);
            CloseThreadpoolWait(wait);
        }
    };

    struct Pipe {
        UniqueHandle read;
        UniqueHandle write;
    };

    // Only this user, at medium integrity or above, may open the pipe. Its name is predictable and it has one instance,
    // so the default descriptor (read for everyone) lets any process take it before this one opens the write end.
    UniqueReleased<HLOCAL, &LocalFree> OwnerOnlyDescriptor() {
        std::vector<std::byte> user   = TokenBuffer(GetCurrentProcessToken(), TokenUser, "TokenUser");
        LPWSTR                 rawSid = nullptr;
        if (!ConvertSidToStringSidW(UserSid(user), &rawSid)) {
            ThrowLastError("ConvertSidToStringSidW");
        }
        const UniqueReleased<HLOCAL, &LocalFree> sid(static_cast<HLOCAL>(rawSid));
        const std::wstring                       sddl       = std::format(L"D:P(A;;GA;;;{})S:(ML;;NWNR;;;ME)", rawSid);
        PSECURITY_DESCRIPTOR                     descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
            ThrowLastError("ConvertStringSecurityDescriptorToSecurityDescriptorW(command output)");
        }
        return UniqueReleased<HLOCAL, &LocalFree>(static_cast<HLOCAL>(descriptor));
    }

    // A named pipe rather than CreatePipe's, whose read end cannot be overlapped, so a read can be abandoned while a
    // process outside the job still holds the write end. The write end is inheritable, for the child's stdout. The
    // read end stays with this process.
    Pipe OutputPipe() {
        static std::atomic<std::uint64_t>        created{0};
        const std::wstring                       name       = std::format(L"\\\\.\\pipe\\intarsia-command-{}-{}", GetCurrentProcessId(), created.fetch_add(1));
        const UniqueReleased<HLOCAL, &LocalFree> descriptor = OwnerOnlyDescriptor();
        SECURITY_ATTRIBUTES                      ownerOnly{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = descriptor.get(), .bInheritHandle = FALSE};
        UniqueHandle                             read = OwnHandle(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                                                   PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 4096, 0, &ownerOnly),
                                                                  "CreateNamedPipeW(command output)");
        SECURITY_ATTRIBUTES                      inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
        UniqueHandle write = OwnHandle(CreateFileW(name.c_str(), GENERIC_WRITE, 0, &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), "CreateFileW(command output)");
        return Pipe{.read = std::move(read), .write = std::move(write)};
    }

    UniqueHandle KillOnCloseJob() {
        UniqueHandle                         job = OwnHandle(CreateJobObjectW(nullptr, nullptr), "CreateJobObjectW");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            ThrowLastError("SetInformationJobObject(kill on close)");
        }
        return job;
    }

    // Suspended, so it is in the job before it can start anything. The handle list keeps every other inheritable handle
    // in this process out of the child.
    PROCESS_INFORMATION StartSuspended(const Command& command, HANDLE output) {
        SIZE_T size = 0;
        // Fails by design on this first call, reporting the size the list needs.
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<std::byte> storage(size);
        auto*                  list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            ThrowLastError("InitializeProcThreadAttributeList");
        }
        const UniqueReleased<LPPROC_THREAD_ATTRIBUTE_LIST, &DeleteProcThreadAttributeList> owned(list);
        HANDLE                                                                             inherited = output;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, static_cast<void*>(&inherited), sizeof(inherited), nullptr, nullptr)) {
            ThrowLastError("UpdateProcThreadAttribute(handle list)");
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb         = sizeof(startup);
        startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = output;
        startup.lpAttributeList        = list;
        const std::wstring  program    = command.program.wstring();
        const std::wstring  arguments  = JoinArguments(command.arguments);
        std::wstring        line       = arguments.empty() ? QuoteArgument(program) : QuoteArgument(program) + L" " + arguments;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                            &startup.StartupInfo, &process)) {
            ThrowLastError("CreateProcessW");
        }
        return process;
    }

    // Negative, which the thread pool reads as relative to now, in 100 ns units.
    FILETIME RelativeDueTime(std::chrono::milliseconds timeout) {
        ULARGE_INTEGER due{};
        due.QuadPart = static_cast<ULONGLONG>(-std::chrono::duration_cast<std::chrono::duration<LONGLONG, std::ratio<1, 10'000'000>>>(timeout).count());
        return FILETIME{.dwLowDateTime = due.LowPart, .dwHighDateTime = due.HighPart};
    }

    // Blocks until the read into chunk completes or stopped is signaled, which abandons it. The byte count read, or
    // nullopt at the end of the pipe or on stop.
    std::optional<DWORD> ReadChunk(HANDLE pipe, std::array<char, 4096>& chunk, HANDLE ready, HANDLE stopped) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = ready;
        if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), nullptr, &overlapped)) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                return std::nullopt;
            }
            if (error != ERROR_IO_PENDING) {
                ThrowLastError("ReadFile(command output)");
            }
        }
        const std::array<HANDLE, 2> waits{ready, stopped};
        const DWORD                 woke      = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
        const DWORD                 waitError = woke == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        if (woke != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &overlapped);
        }
        // Waits out a cancelled read, which must finish before overlapped and chunk go out of scope, a failed wait included.
        DWORD      read     = 0;
        const BOOL finished = GetOverlappedResult(pipe, &overlapped, &read, TRUE);
        if (waitError != ERROR_SUCCESS) {
            throw Win32Error("WaitForMultipleObjects(command output)", HRESULT_FROM_WIN32(waitError));
        }
        if (!finished) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED) {
                return std::nullopt;
            }
            ThrowLastError("GetOverlappedResult(command output)");
        }
        return woke == WAIT_OBJECT_0 ? std::optional<DWORD>(read) : std::nullopt;
    }

    // Everything written until every holder of the write end has closed it or stopped is signaled. Nullopt once it
    // passes maxBytes.
    std::optional<std::string> ReadUpTo(HANDLE pipe, size_t maxBytes, HANDLE stopped) {
        const UniqueHandle     ready = OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(command output)");
        std::string            output;
        std::array<char, 4096> chunk{};
        while (const std::optional<DWORD> read = ReadChunk(pipe, chunk, ready.get(), stopped)) {
            if (output.size() + *read > maxBytes) {
                return std::nullopt;
            }
            output.append(chunk.data(), *read);
        }
        return output;
    }

    // Calls onLine with each line, without its line ending, as it completes, and with a last one the output ends
    // without a line ending unless stopped is signaled. False once a line passes maxLineBytes.
    bool ReadLines(HANDLE pipe, size_t maxLineBytes, HANDLE stopped, const std::function<void(std::string_view)>& onLine) {
        const UniqueHandle     ready = OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(command output)");
        std::string            line;
        std::array<char, 4096> chunk{};
        while (const std::optional<DWORD> read = ReadChunk(pipe, chunk, ready.get(), stopped)) {
            std::string_view rest(chunk.data(), *read);
            for (size_t end = rest.find('\n'); end != std::string_view::npos; end = rest.find('\n')) {
                line.append(rest.substr(0, end));
                if (line.size() > maxLineBytes) {
                    return false;
                }
                if (line.ends_with('\r')) {
                    line.pop_back();
                }
                onLine(line);
                line.clear();
                rest.remove_prefix(end + 1);
            }
            line.append(rest);
            if (line.size() > maxLineBytes) {
                return false;
            }
        }
        if (!line.empty() && WaitForSingleObject(stopped, 0) != WAIT_OBJECT_0) {
            onLine(line);
        }
        return true;
    }

    struct CommandEnd {
        Stop          stop;
        std::uint32_t exitCode;
    };

    // One run of a command, suspended until it is in a kill-on-close job, with its stdout on a pipe only this process
    // reads. The watchdog kills the job when cancel is signaled or the timeout, if any, passes. Whatever is left of the
    // run dies with this.
    class CommandRun {
      public:
        CommandRun(const Command& command, HANDLE cancel, std::optional<std::chrono::milliseconds> timeout) :
            _pipe(OutputPipe()), _job(KillOnCloseJob()), _stopped(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(command stopped)")),
            _watchdog{.job = _job.get(), .stopped = _stopped.get(), .stop = Stop::None}, _wait(CreateThreadpoolWait(StopJob, &_watchdog, nullptr)) {
            if (_wait == nullptr) {
                ThrowLastError("CreateThreadpoolWait");
            }
            const PROCESS_INFORMATION started = StartSuspended(command, _pipe.write.get());
            _process.reset(started.hProcess);
            const UniqueHandle thread(started.hThread);
            // Only the child's copy of the write end is left, so the read ends once the child and whatever it started exit.
            _pipe.write.reset();
            if (!AssignProcessToJobObject(_job.get(), _process.get())) {
                const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
                TerminateProcess(_process.get(), kKilledExitCode);
                throw Win32Error("AssignProcessToJobObject", error);
            }
            if (timeout.has_value()) {
                FILETIME due = RelativeDueTime(*timeout);
                SetThreadpoolWait(_wait.get(), cancel, &due);
            } else {
                SetThreadpoolWait(_wait.get(), cancel, nullptr);
            }
            if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
                ThrowLastError("ResumeThread");
            }
        }
        CommandRun(const CommandRun&)            = delete;
        CommandRun& operator=(const CommandRun&) = delete;

        HANDLE      Output() const {
            return _pipe.read.get();
        }

        // Signaled once the watchdog has killed the job.
        HANDLE Stopped() const {
            return _stopped.get();
        }

        void Kill(std::string_view reason) const {
            if (!TerminateJobObject(_job.get(), kKilledExitCode)) {
                ThrowLastError(std::format("TerminateJobObject({})", reason));
            }
        }

        // Waits for the command to exit, which the watchdog bounds by the timeout and cancel, then disarms it.
        CommandEnd Finish() {
            if (WaitForSingleObject(_process.get(), INFINITE) != WAIT_OBJECT_0) {
                ThrowLastError("WaitForSingleObject(command)");
            }
            _wait.reset();
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(_process.get(), &exitCode)) {
                ThrowLastError("GetExitCodeProcess");
            }
            return CommandEnd{.stop = _watchdog.stop.load(), .exitCode = exitCode};
        }

      private:
        Pipe         _pipe;
        UniqueHandle _job;
        UniqueHandle _stopped;
        Watchdog     _watchdog;
        UniqueHandle _process;
        // Declared last so it is disarmed before the Watchdog it points at goes.
        std::unique_ptr<TP_WAIT, ThreadpoolWaitCloser> _wait;
    };

} // namespace

std::string CaptureOutput(const Command& command, std::chrono::milliseconds timeout, size_t maxBytes, HANDLE cancel) {
    CommandRun                       run(command, cancel, timeout);
    const std::optional<std::string> output = ReadUpTo(run.Output(), maxBytes, run.Stopped());
    if (!output.has_value()) {
        run.Kill("output too long");
    }
    const CommandEnd end = run.Finish();
    switch (end.stop) {
        case Stop::Cancelled: throw CommandError("cancelled");
        case Stop::TimedOut: throw CommandError(std::format("did not finish within {}", timeout));
        case Stop::None: break;
    }
    if (!output.has_value()) {
        throw CommandError(std::format("printed more than {} bytes", maxBytes));
    }
    if (end.exitCode != 0) {
        throw CommandError(std::format("exited with code {}", end.exitCode));
    }
    return *output;
}

std::uint32_t StreamLines(const Command& command, size_t maxLineBytes, HANDLE cancel, const std::function<void(std::string_view)>& onLine) {
    CommandRun run(command, cancel, std::nullopt);
    const bool fits = ReadLines(run.Output(), maxLineBytes, run.Stopped(), onLine);
    if (!fits) {
        run.Kill("line too long");
    }
    const CommandEnd end = run.Finish();
    switch (end.stop) {
        case Stop::Cancelled: throw CommandError("cancelled");
        case Stop::TimedOut: throw std::logic_error("a streamed command has no timeout to pass");
        case Stop::None: break;
    }
    if (!fits) {
        throw CommandError(std::format("printed a line longer than {} bytes", maxLineBytes));
    }
    return end.exitCode;
}

std::optional<std::filesystem::path> ResolveProgram(std::wstring_view program) {
    const std::filesystem::path path(program);
    if (path.is_absolute()) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? std::optional<std::filesystem::path>(path) : std::nullopt;
    }
    // SearchPathW resolves a name with any directory part against the current directory, whatever path it is given.
    if (path.has_parent_path()) {
        return std::nullopt;
    }
    // An explicit search path keeps SearchPathW from also looking in the current directory.
    const std::wstring         searchPath = AbsoluteEntries(PathVariable());
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

Launcher::Launcher() : _queue(kMaxQueuedLaunches, [] { return WorkQueue<Command>::Process(Start); }) {}

void Launcher::Launch(const Command& command) {
    if (!_queue.Push(command)) {
        Log(LogLevel::Warning, "launcher", "launch_dropped", {{.key = "reason", .value = "queue full"}, {.key = "program", .value = ToUtf8(command.program.wstring())}});
    }
}
