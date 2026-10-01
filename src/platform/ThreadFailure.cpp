#include "platform/ThreadFailure.hpp"

#include "platform/Log.hpp"

#include <utility>

ThreadFailure::ThreadFailure(HWND target, std::uint32_t message, std::string component) : _target(target), _message(message), _component(std::move(component)) {}

void ThreadFailure::Report(const std::exception_ptr& error) {
    {
        const std::scoped_lock lock(_mutex);
        if (_error) {
            return;
        }
        _error = error;
    }
    // The UI thread then never learns that the worker stopped, so the log is the only record.
    if (!PostMessageW(_target, _message, 0, 0)) {
        Log(LogLevel::Error, _component, "failure_unreported", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}

void ThreadFailure::RethrowReported() {
    std::exception_ptr error;
    {
        const std::scoped_lock lock(_mutex);
        error = std::exchange(_error, nullptr);
    }
    if (!error) {
        Log(LogLevel::Warning, _component, "spurious_failure", {});
        return;
    }
    std::rethrow_exception(error);
}
