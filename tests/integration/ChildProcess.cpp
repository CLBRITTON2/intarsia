#include "ChildProcess.hpp"

#include "platform/Errors.hpp"
#include "platform/Process.hpp"

#include <windows.h>

#include <format>
#include <string>
#include <vector>

namespace integration {

    namespace {

        UniqueHandle InheritableFile(const std::filesystem::path& path) {
            SECURITY_ATTRIBUTES inherit{
                .nLength = sizeof(SECURITY_ATTRIBUTES),
                .lpSecurityDescriptor = nullptr,
                .bInheritHandle = TRUE,
            };
            return OwnHandle(
                CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr),
                "CreateFileW(child output)");
        }

        struct ProcessWindows {
            DWORD processId;
            std::vector<HWND> windows;
        };

        BOOL CALLBACK CollectProcessWindow(HWND window, LPARAM collection) {
            auto* collected = reinterpret_cast<ProcessWindows*>(collection); // NOLINT(performance-no-int-to-ptr)
            DWORD owner = 0;
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
        std::vector<std::wstring> programAndArguments{program.wstring()};
        programAndArguments.insert(programAndArguments.end(), arguments.begin(), arguments.end());
        std::wstring commandLine = JoinArguments(programAndArguments);
        const UniqueHandle output = InheritableFile(stdoutPath);
        const UniqueHandle errors = InheritableFile(stderrPath);
        SECURITY_ATTRIBUTES inherit{
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE,
        };
        HANDLE readEnd = nullptr;
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
        STARTUPINFOW startup{};
        PROCESS_INFORMATION started{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.get();
        startup.hStdOutput = output.get();
        startup.hStdError = errors.get();
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
        if (WaitForSingleObject(_process.get(), 0) == WAIT_TIMEOUT) {
            TerminateProcess(_process.get(), 1);
            WaitForSingleObject(_process.get(), INFINITE);
        }
    }

    void ChildProcess::WriteLine(std::string_view line) const {
        const std::string text = std::string(line) + "\n";
        DWORD written = 0;
        if (!WriteFile(_input.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size()) {
            ThrowLastError(std::format("WriteFile(child stdin, '{}')", line));
        }
    }

} // namespace integration
