#include "platform/Log.hpp"

#include <windows.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

    std::string ReadAll(const std::filesystem::path& path) {
        const std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

} // namespace

// The sink is process-wide, so the rest of the run logs into this file too, which the test leaves in the temp directory.
TEST(Log, TheFileKeepsTheLastRunsLogAndGetsEveryLineStamped) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("intarsia-log-test-" + std::to_string(GetCurrentProcessId()));
    const std::filesystem::path path = directory / "intarsia.log";
    std::filesystem::create_directories(directory);
    std::ofstream(path, std::ios::binary) << "the last run\n";

    LogToFile(path);
    Log(LogLevel::Warning, "test", "written", {{.key = "value", .value = "one\ntwo"}});

    EXPECT_EQ(ReadAll(directory / "intarsia.1.log"), "the last run\n");
    const std::string written = ReadAll(path);
    // [2026-09-28T17:38:07.062Z], then the line as stderr has it, its value escaped onto one line.
    EXPECT_EQ(written.find("Z]"), 24u);
    EXPECT_NE(written.find(R"(Z]  WARN test         written value="one\x0atwo")"), std::string::npos);
}

TEST(Log, PrintsAPlainValueAsWrittenAndQuotesAnyOther) {
    const std::string line = LogHead(LogLevel::Error, "app", "fatal");
    EXPECT_EQ(line.substr(27), "ERROR app          fatal");

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("intarsia-log-values-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    LogToFile(directory / "intarsia.log");
    Log(LogLevel::Info, "test", "values",
        {{.key = "path", .value = R"(C:\Temp\bar.log)"},
         {.key = "spaced", .value = R"(C:\Program Files)"},
         {.key = "quote", .value = R"(say "hi")"},
         {.key = "empty", .value = ""},
         {.key = "equals", .value = "a=b"},
         // U+202E as bytes, since a string literal holding it fails misc-misleading-bidirectional.
         {.key = "bidi", .value = std::string{'a', '\xE2', '\x80', '\xAE', 'b'}},
         {.key = "long", .value = std::string(2000, 'a')}});

    // Named outside the macro, since MSVC's preprocessor misreads this raw string as a macro argument.
    const std::string expected = R"(values path=C:\Temp\bar.log spaced="C:\\Program Files" quote="say \"hi\"" empty="" equals="a=b" bidi="a)" + std::string("\\u202e") +
        R"(b" long=")" + std::string(1000, 'a') + "...(1000 more bytes)\"";
    EXPECT_NE(ReadAll(directory / "intarsia.log").find(expected), std::string::npos);
}

TEST(Log, TheSoakTestsFatalPatternMatchesAFatalLine) {
    // The pattern scripts/run-soak-tests.ps1 reads each bar log with.
    const std::regex fatal(R"(^\[\S+\]\s+ERROR\s+\S+\s+fatal\b)");
    EXPECT_TRUE(std::regex_search(LogHead(LogLevel::Error, "app", "fatal"), fatal));
    EXPECT_FALSE(std::regex_search(LogHead(LogLevel::Warning, "app", "fatal"), fatal));
}
