#include "platform/Log.hpp"

#include "platform/Text.hpp"

#include <cstdio>
#include <format>

namespace {

    constexpr size_t kValueExcerptBytes = 300;

    std::string_view LevelName(LogLevel level) {
        switch (level) {
            case LogLevel::Info: return "info";
            case LogLevel::Warning: return "warning";
            case LogLevel::Error: return "error";
        }
        return "unknown";
    }

} // namespace

void Log(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields) {
    std::string line = std::format("intarsia: {} component={} event={}", LevelName(level), component, event);
    for (const LogField& field : fields) {
        line += std::format(" {}=\"{}\"", field.key, LogExcerpt(field.value, kValueExcerptBytes));
    }
    line += '\n';
    std::fputs(line.c_str(), stderr);
}

FailureLog::FailureLog(std::string_view component) : _component(component) {}

void FailureLog::Failed(std::string_view event, const std::exception& error) {
    if (!_failing) {
        Log(LogLevel::Warning, _component, event, {{.key = "reason", .value = error.what()}});
    }
    _failing = true;
}

void FailureLog::Succeeded() {
    _failing = false;
}
