#include "platform/Process.hpp"

#include <gtest/gtest.h>

#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <string>
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

TEST(Process, MissingProgramsDoNotResolve) {
    EXPECT_FALSE(ResolveProgram(L"intarsia-no-such-program").has_value());
    EXPECT_FALSE(ResolveProgram(L"C:\\intarsia\\no-such-program.exe").has_value());
}
