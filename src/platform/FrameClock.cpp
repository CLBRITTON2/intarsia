#include "platform/FrameClock.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"

#include <dwmapi.h>

#include <array>
#include <string>
#include <utility>

FrameClock::FrameClock(HWND target, std::uint32_t frameMessage, std::uint32_t failedMessage) :
    _target(target), _frameMessage(frameMessage), _failedMessage(failedMessage), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(frame stop)")),
    _runningEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(frame running)")), _thread([this] { Run(); }) {}

FrameClock::~FrameClock() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

void FrameClock::Start() {
    if (!SetEvent(_runningEvent.get())) {
        ThrowLastError("SetEvent(frame running)");
    }
}

void FrameClock::Stop() {
    if (!ResetEvent(_runningEvent.get())) {
        ThrowLastError("ResetEvent(frame running)");
    }
}

void FrameClock::Acknowledge() {
    _pending.store(false);
}

std::exception_ptr FrameClock::TakeError() {
    const std::scoped_lock lock(_mutex);
    return std::exchange(_error, nullptr);
}

// The running event stays set while started, so each pass waits one composition in DwmFlush.
void FrameClock::Run() {
    const std::array<HANDLE, 2> events{_stopEvent.get(), _runningEvent.get()};
    try {
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0) {
                return;
            }
            if (woke != WAIT_OBJECT_0 + 1) {
                ThrowLastError("WaitForMultipleObjects(frame)");
            }
            ThrowIfFailed(DwmFlush(), "DwmFlush");
            if (!_pending.exchange(true) && !PostMessageW(_target, _frameMessage, 0, 0)) {
                ThrowLastError("PostMessageW(frame)");
            }
        }
    } catch (...) {
        {
            const std::scoped_lock lock(_mutex);
            _error = std::current_exception();
        }
        if (!PostMessageW(_target, _failedMessage, 0, 0)) {
            Log(LogLevel::Error, "frame_clock", "failure_unreported", {{.key = "error", .value = std::to_string(GetLastError())}});
        }
    }
}
