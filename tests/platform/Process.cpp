#include "platform/Handle.hpp"
#include "platform/Process.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    // What a program started with this command line sees as its arguments, split the way the C runtime splits them.
    std::vector<std::wstring> RoundTrip(const std::vector<std::wstring>& arguments) {
        const std::wstring                                  line  = L"program.exe " + JoinArguments(arguments);
        int                                                 count = 0;
        const std::unique_ptr<LPWSTR, decltype(&LocalFree)> argv(CommandLineToArgvW(line.c_str(), &count), &LocalFree);
        std::vector<std::wstring>                           split;
        for (int index = 1; index < count; ++index) {
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

} // namespace

TEST(Process, PlainArgumentsAreLeftAsTheyAre) {
    EXPECT_EQ(JoinArguments({L"-File", L"script.ps1"}), L"-File script.ps1");
}

TEST(Process, ArgumentsSurviveTheRoundTripUnchanged) {
    const std::vector<std::wstring> arguments{L"with space", L"", L"quote\"inside", L"trailing\\", L"C:\\Program Files\\", L"back\\\\\"quote", L"tab\there"};
    EXPECT_EQ(RoundTrip(arguments), arguments);
}

TEST(Process, ShellMetacharactersAreJustText) {
    // No shell parses the line, so these reach the program as literal arguments.
    const std::vector<std::wstring> arguments{L"a&b", L"|", L"%PATH%", L"^"};
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
