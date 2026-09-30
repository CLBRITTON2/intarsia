#include "platform/Handle.hpp"
#include "platform/Process.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellapi.h>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    // What a program started with this command line sees as its arguments, split the way the C runtime splits them.
    std::vector<std::wstring> RoundTrip(const std::vector<std::wstring>& arguments) {
        const std::wstring                                  line  = L"program.exe " + JoinArguments(arguments);
        std::int32_t                                        count = 0;
        const std::unique_ptr<LPWSTR, decltype(&LocalFree)> argv(CommandLineToArgvW(line.c_str(), &count), &LocalFree);
        std::vector<std::wstring>                           split;
        for (std::int32_t index = 1; index < count; ++index) {
            split.emplace_back(argv.get()[index]);
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
        return OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(test cancel)");
    }

    auto CommandErrorWith(std::string_view text) {
        return testing::ThrowsMessage<CommandError>(testing::HasSubstr(std::string(text)));
    }

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

TEST(Process, ProgramsResolveThroughPath) {
    const std::optional<std::filesystem::path> cmd = ResolveProgram(L"cmd");
    ASSERT_TRUE(cmd.has_value());
    if (cmd.has_value()) {
        EXPECT_TRUE(cmd->is_absolute());
        EXPECT_EQ(cmd->filename(), L"cmd.exe");
    }
}

TEST(Process, CaptureOutputReturnsWhatTheCommandPrinted) {
    EXPECT_EQ(CaptureOutput(Cmd({L"/c", L"echo hello"}), std::chrono::seconds(10), 1024, NeverSignaled().get()), "hello\r\n");
}

TEST(Process, CaptureOutputFailsOnANonzeroExit) {
    EXPECT_THAT([] { return CaptureOutput(Cmd({L"/c", L"exit 3"}), std::chrono::seconds(10), 1024, NeverSignaled().get()); }, CommandErrorWith("exited with code 3"));
}

TEST(Process, CaptureOutputKillsACommandPastItsTimeout) {
    EXPECT_THAT([] { return CaptureOutput(Ping(), std::chrono::milliseconds(200), 1024, NeverSignaled().get()); }, CommandErrorWith("did not finish within 200ms"));
}

TEST(Process, CaptureOutputStopsWhenCancelled) {
    const UniqueHandle cancel = NeverSignaled();
    SetEvent(cancel.get());
    EXPECT_THAT([&cancel] { return CaptureOutput(Ping(), std::chrono::seconds(10), 1024, cancel.get()); }, CommandErrorWith("cancelled"));
}

TEST(Process, CaptureOutputFailsPastTheOutputCap) {
    EXPECT_THAT([] { return CaptureOutput(Cmd({L"/c", L"for /l %i in (1,1,200) do @echo 0123456789"}), std::chrono::seconds(10), 100, NeverSignaled().get()); },
                CommandErrorWith("printed more than 100 bytes"));
}

TEST(Process, MissingProgramsDoNotResolve) {
    EXPECT_FALSE(ResolveProgram(L"intarsia-no-such-program").has_value());
    EXPECT_FALSE(ResolveProgram(L"C:\\intarsia\\no-such-program.exe").has_value());
}

// SearchPathW resolves a name with a directory part against the current directory, so such names never resolve, even
// when the file exists there.
TEST(Process, RelativePathsWithDirectoriesDoNotResolve) {
    const std::filesystem::path cmd = SystemProgram(L"cmd");
    EXPECT_TRUE(std::filesystem::exists(cmd));
    const std::wstring relative = std::filesystem::relative(cmd, std::filesystem::current_path()).wstring();
    EXPECT_FALSE(ResolveProgram(relative).has_value());
    EXPECT_FALSE(ResolveProgram(L".\\cmd.exe").has_value());
    EXPECT_FALSE(ResolveProgram(L"System32\\cmd.exe").has_value());
    EXPECT_FALSE(ResolveProgram(L"\\Windows\\System32\\cmd.exe").has_value());
    EXPECT_FALSE(ResolveProgram(L"C:cmd.exe").has_value());
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
