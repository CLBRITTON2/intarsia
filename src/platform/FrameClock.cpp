#include "platform/FrameClock.hpp"

#include "platform/Errors.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <utility>

FrameClock::FrameClock(HWND target, std::uint32_t frameMessage, std::uint32_t failedMessage, Microsoft::WRL::ComPtr<IDXGIOutput> output) :
    _target(target), _frameMessage(frameMessage), _output(std::move(output)), _runningEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(frame running)")),
    _failure(target, failedMessage, "frame_clock"), _thread("frame", [this](HANDLE stopEvent) { Run(stopEvent); }) {}

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

void FrameClock::RethrowFailure() {
    _failure.RethrowReported();
}

std::chrono::steady_clock::time_point FrameClock::LastVBlank() const {
    return _lastVBlank.load();
}

// The running event stays set while started, so each pass waits one vertical blank.
void FrameClock::Run(HANDLE stopEvent) {
    const std::array<HANDLE, 2> events{stopEvent, _runningEvent.get()};
    try {
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0) {
                return;
            }
            if (woke != WAIT_OBJECT_0 + 1) {
                ThrowLastError("WaitForMultipleObjects(frame)");
            }
            // The output is the wait's only input, so its failure means the monitor went.
            const HRESULT waited = _output->WaitForVBlank();
            if (FAILED(waited)) {
                throw MonitorGoneError(std::format("IDXGIOutput::WaitForVBlank failed with 0x{:08X}", static_cast<std::uint32_t>(waited)));
            }
            // Stored before the post, so the frame it asks for reads this blank or a later one.
            _lastVBlank.store(std::chrono::steady_clock::now());
            if (!_pending.exchange(true) && !PostMessageW(_target, _frameMessage, 0, 0)) {
                ThrowLastError("PostMessageW(frame)");
            }
        }
    } catch (...) { _failure.Report(std::current_exception()); }
}
