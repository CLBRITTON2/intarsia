#include "platform/Process.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

#include <objbase.h>
#include <shellapi.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <type_traits>
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

    enum class Stop : std::uint8_t {
        None,
        TimedOut,
        Cancelled
    };

    struct Watchdog {
        HANDLE            job;
        std::atomic<Stop> stop;
    };

    // Runs on a thread pool thread once cancel is signaled or the timeout passes, whichever comes first.
    void CALLBACK StopJob(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT, TP_WAIT_RESULT result) {
        auto* watchdog = static_cast<Watchdog*>(context);
        watchdog->stop.store(result == WAIT_TIMEOUT ? Stop::TimedOut : Stop::Cancelled);
        TerminateJobObject(watchdog->job, kKilledExitCode);
    }

    // Disarms first and waits out a callback already running, so the Watchdog it points at is never used after this.
    struct ThreadpoolWaitCloser {
        void operator()(PTP_WAIT wait) const {
            SetThreadpoolWait(wait, nullptr, nullptr);
            WaitForThreadpoolWaitCallbacks(wait, TRUE);
            CloseThreadpoolWait(wait);
        }
    };

    struct AttributeListDeleter {
        void operator()(LPPROC_THREAD_ATTRIBUTE_LIST list) const {
            DeleteProcThreadAttributeList(list);
        }
    };

    struct Pipe {
        UniqueHandle read;
        UniqueHandle write;
    };

    // The write end is inheritable, for the child's stdout. The read end stays with this process.
    Pipe OutputPipe() {
        SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
        HANDLE              read  = nullptr;
        HANDLE              write = nullptr;
        if (!CreatePipe(&read, &write, &inheritable, 0)) {
            ThrowLastError("CreatePipe(command output)");
        }
        Pipe pipe{.read = UniqueHandle(read), .write = UniqueHandle(write)};
        if (!SetHandleInformation(pipe.read.get(), HANDLE_FLAG_INHERIT, 0)) {
            ThrowLastError("SetHandleInformation(command output)");
        }
        return pipe;
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
        const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, AttributeListDeleter> owned(list);
        HANDLE                                                                                           inherited = output;
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

    // Everything written until every holder of the write end has closed it. Nullopt once it passes maxBytes.
    std::optional<std::string> ReadUpTo(HANDLE pipe, size_t maxBytes) {
        std::string            output;
        std::array<char, 4096> chunk{};
        while (true) {
            DWORD read = 0;
            if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    return output;
                }
                ThrowLastError("ReadFile(command output)");
            }
            if (output.size() + read > maxBytes) {
                return std::nullopt;
            }
            output.append(chunk.data(), read);
        }
    }

} // namespace

std::string CaptureOutput(const Command& command, std::chrono::milliseconds timeout, size_t maxBytes, HANDLE cancel) {
    Pipe                      pipe    = OutputPipe();
    const UniqueHandle        job     = KillOnCloseJob();
    const PROCESS_INFORMATION started = StartSuspended(command, pipe.write.get());
    const UniqueHandle        process(started.hProcess);
    const UniqueHandle        thread(started.hThread);
    // Only the child's copy of the write end is left, so the read ends once the child and whatever it started exit.
    pipe.write.reset();
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
        TerminateProcess(process.get(), kKilledExitCode);
        throw Win32Error("AssignProcessToJobObject", error);
    }

    Watchdog                                       watchdog{.job = job.get(), .stop = Stop::None};
    std::unique_ptr<TP_WAIT, ThreadpoolWaitCloser> wait(CreateThreadpoolWait(StopJob, &watchdog, nullptr));
    if (wait == nullptr) {
        ThrowLastError("CreateThreadpoolWait");
    }
    FILETIME due = RelativeDueTime(timeout);
    SetThreadpoolWait(wait.get(), cancel, &due);
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        ThrowLastError("ResumeThread");
    }

    const std::optional<std::string> output = ReadUpTo(pipe.read.get(), maxBytes);
    if (!output.has_value() && !TerminateJobObject(job.get(), kKilledExitCode)) {
        ThrowLastError("TerminateJobObject(output too long)");
    }
    // The watchdog is still armed, so this wait ends by the timeout at the latest.
    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
        ThrowLastError("WaitForSingleObject(command)");
    }
    wait.reset();

    switch (watchdog.stop.load()) {
        case Stop::Cancelled: throw CommandError("cancelled");
        case Stop::TimedOut: throw CommandError(std::format("did not finish within {}", timeout));
        case Stop::None: break;
    }
    if (!output.has_value()) {
        throw CommandError(std::format("printed more than {} bytes", maxBytes));
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.get(), &exitCode)) {
        ThrowLastError("GetExitCodeProcess");
    }
    if (exitCode != 0) {
        throw CommandError(std::format("exited with code {}", exitCode));
    }
    return *output;
}

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
