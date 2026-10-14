#include "platform/Process.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Text.hpp"
#include "platform/Token.hpp"

#include <sddl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint32_t kKilledExitCode = 1;

    enum class Stop : std::uint8_t {
        None,
        TimedOut,
        Cancelled
    };

    // What each packet on a run's completion port comes from.
    constexpr ULONG_PTR kPipeKey = 1;
    constexpr ULONG_PTR kJobKey  = 2;
    constexpr ULONG_PTR kStopKey = 3;

    struct Watchdog {
        HANDLE            job;
        HANDLE            port; // the stop ends the read there even while a process outside the job holds the pipe
        std::atomic<Stop> stop;
    };

    // The watchdog's wait ends on its timeout or on the cancel event it waits for.
    Stop StopFor(TP_WAIT_RESULT result) {
        if (result == WAIT_TIMEOUT) {
            return Stop::TimedOut;
        }
        return Stop::Cancelled;
    }

    // Runs on a thread pool thread once cancel is signaled or the timeout passes, whichever comes first.
    void CALLBACK StopJob(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT, TP_WAIT_RESULT result) {
        auto* watchdog = static_cast<Watchdog*>(context);
        watchdog->stop.store(StopFor(result));
        TerminateJobObject(watchdog->job, kKilledExitCode);
        PostQueuedCompletionStatus(watchdog->port, 0, kStopKey, nullptr);
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
    UniqueLocal OwnerOnlyDescriptor() {
        std::vector<std::byte> user   = TokenBuffer(GetCurrentProcessToken(), TokenUser, "TokenUser");
        LPWSTR                 rawSid = nullptr;
        if (!ConvertSidToStringSidW(UserSid(user), &rawSid)) {
            ThrowLastError("ConvertSidToStringSidW");
        }
        const UniqueLocal    sid(static_cast<HLOCAL>(rawSid));
        const std::wstring   sddl       = std::format(L"D:P(A;;GA;;;{})S:(ML;;NWNR;;;ME)", rawSid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
            ThrowLastError("ConvertStringSecurityDescriptorToSecurityDescriptorW(command output)");
        }
        return UniqueLocal(static_cast<HLOCAL>(descriptor));
    }

    // A named pipe rather than CreatePipe's, whose read end cannot be overlapped, so a read can be abandoned while a
    // process outside the job still holds the write end. The write end is inheritable, for the child's stdout. The
    // read end stays with this process.
    Pipe OutputPipe() {
        static std::atomic<std::uint64_t> created{0};
        const std::wstring                name       = std::format(L"\\\\.\\pipe\\intarsia-command-{}-{}", GetCurrentProcessId(), created.fetch_add(1));
        const UniqueLocal                 descriptor = OwnerOnlyDescriptor();
        SECURITY_ATTRIBUTES               ownerOnly{
                          .nLength              = sizeof(SECURITY_ATTRIBUTES),
                          .lpSecurityDescriptor = descriptor.get(),
                          .bInheritHandle       = FALSE,
        };
        UniqueHandle        read = OwnHandle(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                              PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 4096, 0, &ownerOnly),
                                             "CreateNamedPipeW(command output)");
        SECURITY_ATTRIBUTES inheritable{
            .nLength              = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle       = TRUE,
        };
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

    // One port for the pipe's reads and the job's messages, which says when no process is left in the job.
    // Associated before any process is in the job, so none of its messages is missed.
    UniqueHandle OutputPort(HANDLE pipe, HANDLE job) {
        UniqueHandle port = OwnHandle(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1), "CreateIoCompletionPort(command output)");
        if (CreateIoCompletionPort(pipe, port.get(), kPipeKey, 0) == nullptr) {
            ThrowLastError("CreateIoCompletionPort(command output pipe)");
        }
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{.CompletionKey = std::bit_cast<PVOID>(kJobKey), .CompletionPort = port.get()};
        if (!SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &association, sizeof(association))) {
            ThrowLastError("SetInformationJobObject(completion port)");
        }
        return port;
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
        startup.StartupInfo.cb            = sizeof(startup);
        startup.StartupInfo.dwFlags       = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput    = output;
        startup.lpAttributeList           = list;
        const std::wstring        program = command.program.wstring();
        std::vector<std::wstring> programAndArguments{program};
        programAndArguments.insert(programAndArguments.end(), command.arguments.begin(), command.arguments.end());
        std::wstring        line = JoinArguments(programAndArguments);
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

    // Reads a run's output through its port, where the job emptying and the watchdog's stop also arrive, so the read
    // ends even while a process outside the job holds the pipe open. The port's only consumer.
    class OutputReader {
      public:
        OutputReader(HANDLE pipe, HANDLE port) : _pipe(pipe), _port(port) {}

        // The byte count read into chunk, or nullopt at the end of the output or once stopped. Once the job is empty,
        // only what the pipe already holds is read.
        std::optional<DWORD> Read(std::array<char, 4096>& chunk) {
            OVERLAPPED overlapped{};
            if (!ReadFile(_pipe, chunk.data(), static_cast<DWORD>(chunk.size()), nullptr, &overlapped)) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) {
                    return std::nullopt;
                }
                if (error != ERROR_IO_PENDING) {
                    ThrowLastError("ReadFile(command output)");
                }
                if (_ended || _stopped) {
                    CancelIoEx(_pipe, &overlapped);
                }
            }
            // The read's own packet must arrive before overlapped and chunk go out of scope, even a read that completed
            // at once, since its packet is queued too.
            while (true) {
                DWORD       bytes     = 0;
                ULONG_PTR   key       = 0;
                OVERLAPPED* completed = nullptr;
                const BOOL  succeeded = GetQueuedCompletionStatus(_port, &bytes, &key, &completed, INFINITE);
                const DWORD error     = GetLastError(); // read below only when succeeded is false
                if (completed == nullptr && !succeeded) {
                    CancelIoEx(_pipe, &overlapped);
                    DWORD read = 0;
                    GetOverlappedResult(_pipe, &overlapped, &read, TRUE);
                    throw Win32Error("GetQueuedCompletionStatus(command output)", HRESULT_FROM_WIN32(error));
                }
                if (key == kPipeKey) {
                    if (succeeded) {
                        if (_stopped) {
                            return std::nullopt;
                        }
                        return bytes;
                    }
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED) {
                        return std::nullopt;
                    }
                    throw Win32Error("ReadFile(command output)", HRESULT_FROM_WIN32(error));
                }
                if (key == kStopKey) {
                    _stopped = true;
                    CancelIoEx(_pipe, &overlapped);
                } else if (key == kJobKey && bytes == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
                    // A read the pipe already had data for completes rather than cancels, so nothing printed is lost.
                    _ended = true;
                    CancelIoEx(_pipe, &overlapped);
                }
            }
        }

        bool Stopped() const {
            return _stopped;
        }

      private:
        HANDLE _pipe;
        HANDLE _port;
        bool   _ended   = false;
        bool   _stopped = false;
    };

    // Everything read until the output ends or stops. Nullopt once it passes maxBytes.
    std::optional<std::string> ReadUpTo(OutputReader& reader, size_t maxBytes) {
        std::string            output;
        std::array<char, 4096> chunk{};
        while (const std::optional<DWORD> read = reader.Read(chunk)) {
            if (output.size() + *read > maxBytes) {
                return std::nullopt;
            }
            output.append(chunk.data(), *read);
        }
        return output;
    }

    // Calls onLine with each line, without its line ending, as it completes, and with a last one the output ends
    // without a line ending unless stopped. False once a line passes maxLineBytes.
    bool ReadLines(OutputReader& reader, size_t maxLineBytes, const std::function<void(std::string_view)>& onLine) {
        std::string            line;
        std::array<char, 4096> chunk{};
        while (const std::optional<DWORD> read = reader.Read(chunk)) {
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
        if (!line.empty() && !reader.Stopped()) {
            onLine(line);
        }
        return true;
    }

    struct CommandEnd {
        Stop          stop;
        std::uint32_t exitCode;
    };

    // One run of a command, suspended until it is in a kill-on-close job, with its stdout on a pipe only this process
    // reads. The watchdog kills the job when cancel is signaled or the timeout, if any, passes. The output ends once
    // the job is empty, or the pipe closes. Whatever is left of the run dies with this.
    class CommandRun {
      public:
        CommandRun(const Command& command, HANDLE cancel, std::optional<std::chrono::milliseconds> timeout) :
            _pipe(OutputPipe()), _job(KillOnCloseJob()), _port(OutputPort(_pipe.read.get(), _job.get())),
            _watchdog{
                .job  = _job.get(),
                .port = _port.get(),
                .stop = Stop::None,
            },
            _wait(CreateThreadpoolWait(StopJob, &_watchdog, nullptr)) {
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
        CommandRun(const CommandRun&)             = delete;
        CommandRun&  operator=(const CommandRun&) = delete;

        OutputReader Reader() const {
            return OutputReader(_pipe.read.get(), _port.get());
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
        UniqueHandle _port;
        Watchdog     _watchdog;
        UniqueHandle _process;
        // Declared last so it is disarmed before the Watchdog it points at, and the port it posts to, go.
        std::unique_ptr<TP_WAIT, ThreadpoolWaitCloser> _wait;
    };

} // namespace

void RestrictDllSearchToSystem32() {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        ThrowLastError("SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)");
    }
}

std::filesystem::path ProcessImage(HANDLE process) {
    std::array<wchar_t, 32768> path{};
    DWORD                      length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        ThrowLastError("QueryFullProcessImageNameW");
    }
    return {std::wstring(path.data(), length)};
}

std::string CaptureOutput(const Command& command, std::chrono::milliseconds timeout, size_t maxBytes, HANDLE cancel) {
    CommandRun                       run(command, cancel, timeout);
    OutputReader                     reader = run.Reader();
    const std::optional<std::string> output = ReadUpTo(reader, maxBytes);
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
    CommandRun   run(command, cancel, std::nullopt);
    OutputReader reader = run.Reader();
    const bool   fits   = ReadLines(reader, maxLineBytes, onLine);
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

void RequireDrawable(std::string_view text, std::string_view field, std::wstring_view allowed) {
    std::wstring wide;
    try {
        wide = ToWide(text);
    } catch (const Win32Error&) { throw OutputError(std::format("{} is not UTF-8", field)); }
    const auto unsafe = std::ranges::find_if(wide, [allowed](wchar_t unit) { return IsUnsafeCharacter(unit) && allowed.find(unit) == std::wstring_view::npos; });
    if (unsafe != wide.end()) {
        throw OutputError(std::format("{} has control character U+{:04X}", field, static_cast<std::uint32_t>(*unsafe)));
    }
}
