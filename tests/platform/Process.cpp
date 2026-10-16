#include "platform/Process.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "support/ThrowsWith.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

    // What a program started with this command line sees as its arguments, split the way the C runtime splits them.
    std::vector<std::wstring> RoundTrip(const std::vector<std::wstring>& arguments) {
        const std::wstring        line  = L"program.exe " + JoinArguments(arguments);
        std::int32_t              count = 0;
        LPWSTR*                   argv  = CommandLineToArgvW(line.c_str(), &count);
        const UniqueLocal         owned(static_cast<HLOCAL>(argv));
        std::vector<std::wstring> split;
        for (std::int32_t index = 1; index < count; ++index) {
            split.emplace_back(argv[index]);
        }
        return split;
    }

    std::filesystem::path SystemProgram(std::wstring_view name) {
        const std::optional<std::filesystem::path> program = ResolveProgram(name);
        if (!program.has_value()) {
            throw std::runtime_error("test program not on PATH");
        }
        return *program;
    }

    Command Cmd(std::vector<std::wstring> arguments) {
        return Command{.program = SystemProgram(L"cmd"), .arguments = std::move(arguments)};
    }

    // Runs for about four seconds, far past the timeouts the cases use.
    Command Ping() {
        return Command{.program = SystemProgram(L"ping"), .arguments = {L"-n", L"5", L"127.0.0.1"}};
    }

    UniqueHandle NeverSignaled() {
        return ManualResetEvent("test cancel");
    }

    // A process outside any job CaptureOutput makes, killed when this goes.
    class OutsideProcess {
      public:
        OutsideProcess() {
            const std::wstring  program = Ping().program.wstring();
            std::wstring        line    = L"ping -n 60 127.0.0.1";
            STARTUPINFOW        startup{.cb = sizeof(STARTUPINFOW)};
            PROCESS_INFORMATION started{};
            if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started)) {
                throw std::runtime_error("could not start the outside process");
            }
            _process = UniqueHandle(started.hProcess);
            CloseHandle(started.hThread);
            _id = started.dwProcessId;
        }
        ~OutsideProcess() {
            TerminateProcess(_process.get(), 1);
            WaitForSingleObject(_process.get(), INFINITE);
        }
        OutsideProcess(const OutsideProcess&)            = delete;
        OutsideProcess& operator=(const OutsideProcess&) = delete;

        DWORD           Id() const {
            return _id;
        }

      private:
        UniqueHandle _process;
        DWORD        _id = 0;
    };

    constexpr std::wstring_view kStubName = L"intarsia-path-stub";

    // A directory holding an empty kStubName.exe, current for as long as it lives, with PATH and the current directory
    // put back when it goes.
    class StubDirectory {
      public:
        StubDirectory() :
            _path(std::filesystem::temp_directory_path() / ("intarsia-path-test-" + std::to_string(GetCurrentProcessId()))), _previousDirectory(std::filesystem::current_path()),
            _previousPath(Path()) {
            std::filesystem::create_directories(_path);
            std::ofstream(_path / (std::wstring(kStubName) + L".exe")).close();
            std::filesystem::current_path(_path);
        }
        ~StubDirectory() {
            SetEnvironmentVariableW(L"PATH", _previousPath.c_str());
            std::error_code error;
            std::filesystem::current_path(_previousDirectory, error);
            std::filesystem::remove_all(_path, error);
        }
        StubDirectory(const StubDirectory&)                          = delete;
        StubDirectory&               operator=(const StubDirectory&) = delete;

        const std::filesystem::path& Get() const {
            return _path;
        }

      private:
        static std::wstring Path() {
            std::wstring value(32767, L'\0');
            value.resize(GetEnvironmentVariableW(L"PATH", value.data(), static_cast<DWORD>(value.size())));
            return value;
        }

        std::filesystem::path _path;
        std::filesystem::path _previousDirectory;
        std::wstring          _previousPath;
    };

} // namespace

// No shell parses the line, so shell metacharacters reach the program as literal arguments too.
TEST(Process, ArgumentsSurviveTheRoundTripUnchanged) {
    const std::vector<std::wstring> arguments{L"with space", L"",  L"quote\"inside", L"trailing\\", L"C:\\Program Files\\", L"back\\\\\"quote", L"tab\there",
                                              L"a&b",        L"|", L"%PATH%",        L"^"};
    EXPECT_EQ(RoundTrip(arguments), arguments);
}

// SearchPathW resolves a name with a directory part against the current directory, so such names never resolve, even
// when the file exists there.
TEST(Process, MissingProgramsAndRelativePathsWithDirectoriesDoNotResolve) {
    const std::filesystem::path     cmd = SystemProgram(L"cmd");
    const std::vector<std::wstring> names{
        L"intarsia-no-such-program",
        L"C:\\intarsia\\no-such-program.exe",
        std::filesystem::relative(cmd, std::filesystem::current_path()).wstring(),
        L".\\cmd.exe",
        L"System32\\cmd.exe",
        L"\\Windows\\System32\\cmd.exe",
        L"C:cmd.exe",
    };
    for (const std::wstring& name : names) {
        SCOPED_TRACE(name);
        EXPECT_FALSE(ResolveProgram(name).has_value());
    }
}

// A relative PATH entry (".", an empty one, "bin") names wherever the bar happened to start, never a place a program is
// installed.
TEST(Process, RelativePathEntriesAreNotSearched) {
    const StubDirectory directory;
    SetEnvironmentVariableW(L"PATH", (L".;;..\\" + directory.Get().filename().wstring()).c_str());
    EXPECT_FALSE(ResolveProgram(kStubName).has_value());

    SetEnvironmentVariableW(L"PATH", (L".;" + directory.Get().wstring()).c_str());
    const std::optional<std::filesystem::path> found = ResolveProgram(kStubName);
    if (!found.has_value()) {
        FAIL() << "the absolute entry was not searched";
    }
    EXPECT_TRUE(std::filesystem::equivalent(found->parent_path(), directory.Get()));
}

TEST(Process, CaptureOutputFailsOnANonzeroExit) {
    EXPECT_THAT([] { return CaptureOutput(Cmd({L"/c", L"exit 3"}), std::chrono::seconds(10), 1024, NeverSignaled().get()); }, ThrowsWith<CommandError>("exited with code 3"));
}

TEST(Process, CaptureOutputKillsACommandPastItsTimeout) {
    EXPECT_THAT([] { return CaptureOutput(Ping(), std::chrono::milliseconds(200), 1024, NeverSignaled().get()); }, ThrowsWith<CommandError>("did not finish within 200ms"));
}

TEST(Process, CaptureOutputStopsWhenCancelled) {
    const UniqueHandle cancel = NeverSignaled();
    SetEvent(cancel.get());
    EXPECT_THAT([&cancel] { return CaptureOutput(Ping(), std::chrono::seconds(10), 1024, cancel.get()); }, ThrowsWith<CommandError>("cancelled"));
}

// The job cannot kill a process outside it, so the read of a pipe such a process holds open ends with the job.
TEST(Process, CaptureOutputEndsWhenItsCommandExitsWhileAProcessOutsideTheJobHoldsItsOutput) {
    std::future<std::string> captured;
    std::future_status       status = std::future_status::timeout;
    {
        const OutsideProcess holder;
        captured = std::async(std::launch::async, [&holder] {
            return CaptureOutput(Command{.program = INTARSIA_PIPE_HOLDER, .arguments = {std::to_wstring(holder.Id())}}, std::chrono::seconds(10), 1024, NeverSignaled().get());
        });
        status   = captured.wait_for(std::chrono::seconds(5));
    }
    // The holder is gone by now, so a read that waited for the pipe to close has ended too and get cannot hang.
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_EQ(captured.get(), "");
}

// A process the command starts is in its job, so its output still counts after the command exits.
TEST(Process, CaptureOutputWaitsForWhatTheCommandStarted) {
    EXPECT_EQ(CaptureOutput(Cmd({L"/c", L"start /b cmd /c ping -n 2 127.0.0.1 ^>nul ^& echo late"}), std::chrono::seconds(10), 1024, NeverSignaled().get()), "late\r\n");
}

TEST(Process, CaptureOutputFailsPastTheOutputCap) {
    EXPECT_THAT([] { return CaptureOutput(Cmd({L"/c", L"for /l %i in (1,1,200) do @echo 0123456789"}), std::chrono::seconds(10), 100, NeverSignaled().get()); },
                ThrowsWith<CommandError>("printed more than 100 bytes"));
}

TEST(Process, StreamLinesDeliversEachLineAndReturnsTheExitCode) {
    std::vector<std::string> lines;
    const std::uint32_t      exitCode =
        StreamLines(Cmd({L"/c", L"echo one& echo two& exit 4"}), 1024, NeverSignaled().get(), [&lines](std::string_view line) { lines.emplace_back(line); });
    EXPECT_EQ(lines, (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(exitCode, 4u);
}

TEST(Process, StreamLinesDeliversALastLineWithNoLineEnding) {
    std::vector<std::string> lines;
    StreamLines(Cmd({L"/c", L"set /p =partial<nul"}), 1024, NeverSignaled().get(), [&lines](std::string_view line) { lines.emplace_back(line); });
    EXPECT_EQ(lines, (std::vector<std::string>{"partial"}));
}

TEST(Process, StreamLinesFailsPastTheLineCap) {
    EXPECT_THAT([] { return StreamLines(Cmd({L"/c", L"echo 0123456789abcdef"}), 8, NeverSignaled().get(), [](std::string_view) {}); },
                ThrowsWith<CommandError>("printed a line longer than 8 bytes"));
}

TEST(Process, StreamLinesStopsARunningCommandWhenCancelled) {
    const UniqueHandle cancel  = NeverSignaled();
    const auto         started = std::chrono::steady_clock::now();
    EXPECT_THAT([&cancel] { return StreamLines(Cmd({L"/c", L"echo up& ping -n 60 127.0.0.1"}), 1024, cancel.get(), [&cancel](std::string_view) { SetEvent(cancel.get()); }); },
                ThrowsWith<CommandError>("cancelled"));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(30));
}

TEST(Process, RunOrFailureTurnsCommandAndWin32ErrorsIntoAReason) {
    const RunFailure command = std::get<RunFailure>(RunOrFailure([]() -> std::string { throw CommandError("timed out"); }));
    EXPECT_EQ(command.reason, "timed out");
    EXPECT_EQ(command.detail, "timed out");
    const RunFailure win32 = std::get<RunFailure>(RunOrFailure([]() -> std::string { throw Win32Error("CreateProcessW", E_ACCESSDENIED); }));
    EXPECT_EQ(win32.reason, "could not run");
    EXPECT_THAT(win32.detail, testing::HasSubstr("CreateProcessW"));
}
