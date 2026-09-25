#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogField {
    std::string_view key;
    std::string      value;
};

// One structured line on stderr: intarsia: <level> component=<component> event=<event> key="value" ... Every value is
// escaped and cut with LogExcerpt, so text from a peer or a file can neither break the line nor flood the log.
void Log(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields);
