#include "platform/Log.hpp"

#include "platform/Errors.hpp"
#include "platform/KnownFolder.hpp"
#include "platform/Text.hpp"

#include <windows.h>

#include <shlobj.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <share.h>
#include <stdexcept>
#include <string>

namespace {

    constexpr size_t         kValueExcerptBytes = 300;
    constexpr std::uintmax_t kMaxLogBytes       = std::uintmax_t{8} * 1024 * 1024;

    std::string_view         LevelName(LogLevel level) {
        switch (level) {
            case LogLevel::Info: return "info";
            case LogLevel::Warning: return "warning";
            case LogLevel::Error: return "error";
        }
        return "unknown";
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
        const std::string stamped = std::format("{:%FT%T}Z {}", std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()), line);
        std::fputs(stamped.c_str(), sink.file);
        std::fflush(sink.file);
        sink.bytes += stamped.size();
        if (sink.bytes <= kMaxLogBytes) {
            return;
        }
        try {
            StartFile(sink);
        } catch (const std::exception& error) {
            if (sink.file != nullptr) {
                std::fclose(sink.file);
                sink.file = nullptr;
            }
            std::fputs(std::format("intarsia: error component=log event=file_stopped reason=\"{}\"\n", LogExcerpt(error.what(), kValueExcerptBytes)).c_str(), stderr);
        }
    }

    std::string FormatLine(LogLevel level, std::string_view component, std::string_view event, std::initializer_list<LogField> fields) {
        std::string line = std::format("intarsia: {} component={} event={}", LevelName(level), component, event);
        for (const LogField& field : fields) {
            line += std::format(" {}=\"{}\"", field.key, LogExcerpt(field.value, kValueExcerptBytes));
        }
        line += '\n';
        return line;
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
    void LogAbort(std::int32_t /*signal*/) {
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
    std::signal(SIGABRT, [](int signal) { LogAbort(signal); });
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
