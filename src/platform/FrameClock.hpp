#pragma once

#include "platform/Handle.hpp"
#include "platform/StoppableThread.hpp"
#include "platform/ThreadFailure.hpp"

#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

// Posts frameMessage to target once per vertical blank of output while started, so animations draw at that monitor's
// refresh rather than a timer's (DwmFlush would follow one composition clock for every monitor, the primary's). As
// ChangeSignal, it posts at most one message until the UI thread acknowledges it. A failure that stops its thread posts
// failedMessage.
class FrameClock {
  public:
    FrameClock(HWND target, std::uint32_t frameMessage, std::uint32_t failedMessage, Microsoft::WRL::ComPtr<IDXGIOutput> output);
    FrameClock(const FrameClock&)            = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    void        Start();
    void        Stop();
    // The UI thread, once it has drawn the frame, so the next frame posts again at the following vertical blank.
    void Acknowledge();
    // On failedMessage, rethrows the error that stopped the thread.
    void RethrowFailure();

  private:
    void                                Run(HANDLE stopEvent);

    HWND                                _target;
    std::uint32_t                       _frameMessage;
    Microsoft::WRL::ComPtr<IDXGIOutput> _output;
    UniqueHandle                        _runningEvent; // set while started
    std::atomic<bool>                   _pending = false;
    ThreadFailure                       _failure;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};
