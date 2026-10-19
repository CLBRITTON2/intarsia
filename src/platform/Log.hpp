#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

enum class LogLevel : std::uint8_t {
    Info,
    Warning,
    Error
};

struct LogField {
    std::string_view key;
    std::string value;
};

// "[<UTC time>] <level> <component> <event>", the level right-aligned and the component padded, so the events of
// successive lines start in one column.
std::string LogHead(LogLevel level, std::string_view component, std::string_view event);

// One structured line on stderr, and after LogToFile in the log file too: the LogHead, then key=value for each field.
// A value of plain ASCII with no space, quote or equals sign prints as written, any other is quoted, escaped and cut
// with LogExcerpt, so text from a peer or a file can neither break the line nor flood the log. Safe from any thread.
void Log(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields);

// %LOCALAPPDATA%\intarsia\intarsia.log
std::filesystem::path DefaultLogPath();

// Sends every Log line from now on to path as well, flushed line by line so a crash loses none. The last run's log is
// kept as <stem>.1<extension> first, so the log of a run that crashed survives the restart after it, and the file
// starts over the same way once it passes 8 MiB. Throws std::filesystem::filesystem_error or std::runtime_error when
// the directory or file cannot be made.
void LogToFile(const std::filesystem::path& path);

// Logs, as the event fatal, an exception that escapes a thread (std::terminate) and a crash no handler catches, such as an
// access violation, before the process ends as it would have.
void LogCrashes();

// Logs a warning when a retried operation starts failing, and nothing more until it has succeeded again.
class FailureLog {
  public:
    explicit FailureLog(std::string_view component);

    void Failed(std::string_view event, const std::exception& error);
    void Succeeded();

  private:
    std::string_view _component;
    bool _failing = false;
};
