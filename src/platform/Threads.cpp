#include "platform/Threads.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"

#include <windows.h>

#include <objbase.h>

#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace {

    DWORD Apartment(ComThreading threading) {
        if (threading == ComThreading::Single) {
            return COINIT_APARTMENTTHREADED;
        }
        return COINIT_MULTITHREADED;
    }

} // namespace

StoppableThread::StoppableThread(std::string_view name, std::function<void(HANDLE stopEvent)> body) :
    _stopEvent(ManualResetEvent(std::format("{} stop", name))), _thread([stopEvent = _stopEvent.get(), body = std::move(body)] { body(stopEvent); }) {}

StoppableThread::~StoppableThread() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

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

ChangeSignal::ChangeSignal(HWND target, std::uint32_t message) : _target(target), _message(message) {}

void ChangeSignal::Notify(std::string_view component) {
    if (_pending.exchange(true)) {
        return;
    }
    if (!PostMessageW(_target, _message, 0, 0)) {
        const DWORD error = GetLastError();
        _pending = false;
        Log(LogLevel::Warning, component, "post_failed", {{.key = "error", .value = std::to_string(error)}});
    }
}

void ChangeSignal::Acknowledge() {
    _pending = false;
}

ComScope::ComScope(ComThreading threading, std::string_view name) {
    ThrowIfFailed(CoInitializeEx(nullptr, Apartment(threading) | COINIT_DISABLE_OLE1DDE), std::format("CoInitializeEx({})", name));
}

ComScope::~ComScope() {
    CoUninitialize();
}
