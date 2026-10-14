#include "ChildProcess.hpp"

#include "platform/Errors.hpp"

#include <windows.h>

#include <format>

namespace integration {

    namespace {

        // The backslashes written for the run of slashes before unit: before a quote they double, plus one that escapes it.
        size_t EscapedSlashes(wchar_t unit, size_t slashes) {
            if (unit == L'"') {
                return (slashes * 2) + 1;
            }
            return slashes;
        }

        // Windows quotes arguments as CommandLineToArgvW reads them back, so a path holding spaces survives.
        std::wstring Quoted(const std::wstring& argument) {
            std::wstring quoted  = L"\"";
            size_t       slashes = 0;
            for (const wchar_t unit : argument) {
                if (unit == L'\\') {
                    ++slashes;
                    continue;
                }
                quoted.append(EscapedSlashes(unit, slashes), L'\\');
                slashes = 0;
                quoted += unit;
            }
            quoted.append(slashes * 2, L'\\');
            return quoted + L"\"";
        }

        UniqueHandle InheritableFile(const std::filesystem::path& path) {
            SECURITY_ATTRIBUTES inherit{
                .nLength              = sizeof(SECURITY_ATTRIBUTES),
                .lpSecurityDescriptor = nullptr,
                .bInheritHandle       = TRUE,
            };
            return OwnHandle(
                CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr),
                "CreateFileW(child output)");
        }

        UniqueHandle KillOnCloseJob() {
            UniqueHandle                         job = OwnHandle(CreateJobObjectW(nullptr, nullptr), "CreateJobObjectW");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                ThrowLastError("SetInformationJobObject");
            }
            return job;
        }

        struct ProcessWindows {
            DWORD             processId;
            std::vector<HWND> windows;
        };

        BOOL CALLBACK CollectProcessWindow(HWND window, LPARAM collection) {
            auto* collected = reinterpret_cast<ProcessWindows*>(collection); // NOLINT(performance-no-int-to-ptr)
            DWORD owner     = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == collected->processId) {
                collected->windows.push_back(window);
            }
            return TRUE;
        }

        std::vector<HWND> TopLevelWindows(std::uint32_t processId) {
            ProcessWindows collection{.processId = processId, .windows = {}};
            EnumWindows(CollectProcessWindow, reinterpret_cast<LPARAM>(&collection));
            return collection.windows;
        }

    } // namespace

    ChildProcess::ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::filesystem::path& stdoutPath,
                               const std::filesystem::path& stderrPath) : _job(KillOnCloseJob()), _id(0) {
        std::wstring commandLine = Quoted(program.wstring());
        for (const std::wstring& argument : arguments) {
            commandLine += L" " + Quoted(argument);
        }
        const UniqueHandle  output = InheritableFile(stdoutPath);
        const UniqueHandle  errors = InheritableFile(stderrPath);
        SECURITY_ATTRIBUTES inherit{
            .nLength              = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle       = TRUE,
        };
        HANDLE readEnd  = nullptr;
        HANDLE writeEnd = nullptr;
        if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
            ThrowLastError("CreatePipe(child stdin)");
        }
        const UniqueHandle input(readEnd);
        _input = UniqueHandle(writeEnd);
        // Only the child's end is inherited, so the pipe reaches end of file once this side closes.
        if (!SetHandleInformation(_input.get(), HANDLE_FLAG_INHERIT, 0)) {
            ThrowLastError("SetHandleInformation(child stdin)");
        }
        STARTUPINFOW        startup{};
        PROCESS_INFORMATION started{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdInput  = input.get();
        startup.hStdOutput = output.get();
        startup.hStdError  = errors.get();
        if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &started)) {
            ThrowLastError(std::format("CreateProcessW({})", program.string()));
        }
        _process = UniqueHandle(started.hProcess);
        const UniqueHandle thread(started.hThread);
        _id = started.dwProcessId;
        // Suspended until it is in the job, so nothing it starts escapes the kill on close.
        if (!AssignProcessToJobObject(_job.get(), _process.get())) {
            TerminateProcess(_process.get(), 1);
            ThrowLastError("AssignProcessToJobObject");
        }
        ResumeThread(thread.get());
    }

    std::uint32_t ChildProcess::Id() const {
        return _id;
    }

    bool ChildProcess::Running() const {
        return WaitForSingleObject(_process.get(), 0) == WAIT_TIMEOUT;
    }

    std::optional<std::uint32_t> ChildProcess::Close(std::chrono::milliseconds limit) const {
        for (HWND window : TopLevelWindows(_id)) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (WaitForSingleObject(_process.get(), static_cast<DWORD>(limit.count())) != WAIT_OBJECT_0) {
            return std::nullopt;
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(_process.get(), &code)) {
            ThrowLastError("GetExitCodeProcess");
        }
        return static_cast<std::uint32_t>(code);
    }

    void ChildProcess::Kill() const {
        if (Running()) {
            TerminateProcess(_process.get(), 1);
            WaitForSingleObject(_process.get(), INFINITE);
        }
    }

    void ChildProcess::WriteLine(std::string_view line) const {
        const std::string text    = std::string(line) + "\n";
        DWORD             written = 0;
        if (!WriteFile(_input.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size()) {
            ThrowLastError(std::format("WriteFile(child stdin, '{}')", line));
        }
    }

} // namespace integration
