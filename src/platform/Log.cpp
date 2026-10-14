#include "platform/Log.hpp"

#include "platform/KnownFolder.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
#include <share.h>
#include <stdexcept>
#include <string>

namespace {

    constexpr size_t         kValueExcerptBytes = 1000;
    constexpr size_t         kComponentWidth    = 12;
    constexpr std::uintmax_t kMaxLogBytes       = std::uintmax_t{8} * 1024 * 1024;

    std::string_view         LevelName(LogLevel level) {
        switch (level) {
            case LogLevel::Info: return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error: return "ERROR";
        }
        return "UNKNOWN";
    }

    // Plain ASCII with nothing that could end the field, so it reads as written, backslashes included.
    bool PrintsBare(std::string_view value) {
        return !value.empty() && value.size() <= kValueExcerptBytes && std::ranges::none_of(value, [](char character) {
            return character == ' ' || character == '"' || character == '=' || IsControlByte(character) || static_cast<std::uint8_t>(character) >= 0x80u;
        });
    }

    std::string FormatLine(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields) {
        std::string line = LogHead(level, component, event);
        for (const LogField& field : fields) {
            line += PrintsBare(field.value) ? std::format(" {}={}", field.key, field.value) : std::format(" {}=\"{}\"", field.key, LogExcerpt(field.value, kValueExcerptBytes));
        }
        line += '\n';
        return line;
    }

    struct LogFile {
        std::mutex            mutex;
        std::filesystem::path path;
        std::FILE*            file  = nullptr;
        std::uintmax_t        bytes = 0;
    };

    // Every thread logs, so the one file is shared state by nature.
    LogFile& Sink() {
        static LogFile sink;
        return sink;
    }

    std::filesystem::path PreviousPath(const std::filesystem::path& path) {
        return path.parent_path() / (path.stem().wstring() + L".1" + path.extension().wstring());
    }

    // The caller holds the sink's mutex. Other processes may read the file while the bar writes it.
    void StartFile(LogFile& sink) {
        if (sink.file != nullptr) {
            std::fclose(sink.file);
            sink.file = nullptr;
        }
        std::filesystem::create_directories(sink.path.parent_path());
        if (std::filesystem::exists(sink.path)) {
            std::filesystem::rename(sink.path, PreviousPath(sink.path));
        }
        sink.file = _wfsopen(sink.path.c_str(), L"wb", _SH_DENYWR);
        if (sink.file == nullptr) {
            throw std::runtime_error(std::format("could not open the log file {}: errno {}", ToUtf8(sink.path.wstring()), errno));
        }
        sink.bytes = 0;
    }

    // A failure here stops file logging rather than throwing out of a Log call, which runs in catch blocks and on
    // every thread. stderr still says why.
    void WriteToFile(LogFile& sink, const std::string& line) {
        if (sink.file == nullptr) {
            return;
        }
        std::fputs(line.c_str(), sink.file);
        std::fflush(sink.file);
        sink.bytes += line.size();
        if (sink.bytes <= kMaxLogBytes) {
            return;
        }
        try {
            StartFile(sink);
        } catch (const std::exception& error) { std::fputs(FormatLine(LogLevel::Error, "log", "file_stopped", {{.key = "reason", .value = error.what()}}).c_str(), stderr); }
    }

    // A crash can come while its own thread holds the mutex, so the crash path never waits for it.
    void LogCrash(const std::string& line) {
        std::fputs(line.c_str(), stderr);
        LogFile&                           sink = Sink();
        const std::unique_lock<std::mutex> lock(sink.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            WriteToFile(sink, line);
        }
    }

    std::string CurrentExceptionReason(std::string_view none) {
        const std::exception_ptr current = std::current_exception();
        if (!current) {
            return std::string(none);
        }
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) { return error.what(); } catch (...) {
            return "an exception that is not a std::exception";
        }
    }

    // MSVC keeps a terminate handler per thread, so a worker thread's std::terminate reaches only this: abort raises
    // SIGABRT for the whole process, on the thread that aborts, while the exception that ended it is still current.
    void LogAbort() {
        LogCrash(FormatLine(LogLevel::Error, "app", "fatal", {{.key = "reason", .value = CurrentExceptionReason("abort with no exception")}}));
    }

    [[noreturn]] void LogTerminate() {
        LogCrash(FormatLine(LogLevel::Error, "app", "fatal", {{.key = "reason", .value = CurrentExceptionReason("std::terminate with no exception")}}));
        // Past the SIGABRT handler, which would log the same line again.
        std::signal(SIGABRT, SIG_DFL);
        std::abort();
    }

    LONG WINAPI LogUnhandledException(EXCEPTION_POINTERS* exception) {
        const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
        LogCrash(FormatLine(LogLevel::Error, "app", "fatal",
                            {{.key = "reason", .value = "unhandled exception"},
                             {.key = "code", .value = std::format("0x{:08X}", static_cast<std::uint32_t>(record.ExceptionCode))},
                             {.key = "address", .value = std::format("{}", record.ExceptionAddress)}}));
        // Windows Error Reporting still gets the crash, and a dump when LocalDumps asks for one.
        return EXCEPTION_CONTINUE_SEARCH;
    }

} // namespace

std::string LogHead(LogLevel level, std::string_view component, std::string_view event) {
    return std::format("[{:%FT%T}Z] {:>5} {:<{}} {}", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()), LevelName(level), component, kComponentWidth,
                       event);
}

void Log(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields) {
    const std::string line = FormatLine(level, component, event, fields);
    std::fputs(line.c_str(), stderr);
    LogFile&               sink = Sink();
    const std::scoped_lock lock(sink.mutex);
    WriteToFile(sink, line);
}

std::filesystem::path DefaultLogPath() {
    return KnownFolderPath(FOLDERID_LocalAppData, "FOLDERID_LocalAppData") / L"intarsia" / L"intarsia.log";
}

void LogToFile(const std::filesystem::path& path) {
    LogFile&               sink = Sink();
    const std::scoped_lock lock(sink.mutex);
    sink.path = path;
    StartFile(sink);
}

void LogCrashes() {
    std::set_terminate(LogTerminate);
    std::signal(SIGABRT, [](int /*signal*/) { LogAbort(); });
    SetUnhandledExceptionFilter(LogUnhandledException);
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
