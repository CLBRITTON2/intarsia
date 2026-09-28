#include "platform/Log.hpp"

#include <windows.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

    std::string ReadAll(const std::filesystem::path& path) {
        std::ifstream     file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

} // namespace

// The sink is process-wide, so the rest of the run logs into this file too, which the test leaves in the temp directory.
TEST(Log, TheFileKeepsTheLastRunsLogAndGetsEveryLineStamped) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / ("intarsia-log-test-" + std::to_string(GetCurrentProcessId()));
    const std::filesystem::path path      = directory / "intarsia.log";
    std::filesystem::create_directories(directory);
    std::ofstream(path, std::ios::binary) << "the last run\n";

    LogToFile(path);
    Log(LogLevel::Warning, "test", "written", {{.key = "value", .value = "one\ntwo"}});

    EXPECT_EQ(ReadAll(directory / "intarsia.1.log"), "the last run\n");
    const std::string written = ReadAll(path);
    // 2026-09-28T17:38:07.062Z, then the line as stderr has it, its value escaped onto one line.
    EXPECT_EQ(written.find('Z'), 23u);
    EXPECT_NE(written.find(R"(Z intarsia: warning component=test event=written value="one\x0atwo")"), std::string::npos);
}
