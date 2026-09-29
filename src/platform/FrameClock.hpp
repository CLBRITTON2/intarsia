#pragma once

#include "platform/Handle.hpp"

#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>

// Posts frameMessage to target once per vertical blank of output while started, so animations draw at that monitor's
// refresh rather than a timer's (DwmFlush would follow one composition clock for every monitor, the primary's). As
// ChangeSignal, it posts at most one message until the UI thread acknowledges it. A failure that stops its thread posts
// failedMessage.
class FrameClock {
  public:
    FrameClock(HWND target, std::uint32_t frameMessage, std::uint32_t failedMessage, Microsoft::WRL::ComPtr<IDXGIOutput> output);
    ~FrameClock();
    FrameClock(const FrameClock&)            = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    void        Start();
    void        Stop();
    // The UI thread, once it has drawn the frame, so the next frame posts again at the following vertical blank.
    void Acknowledge();
    // The error that stopped the thread, set before failedMessage is posted.
    std::exception_ptr TakeError();

  private:
    void                                Run();

    HWND                                _target;
    std::uint32_t                       _frameMessage;
    std::uint32_t                       _failedMessage;
    Microsoft::WRL::ComPtr<IDXGIOutput> _output;
    UniqueHandle                        _stopEvent;
    UniqueHandle                        _runningEvent; // set while started
    std::atomic<bool>                   _pending = false;
    std::mutex                          _mutex; // guards _error
    std::exception_ptr                  _error;
    // Declared last so every member above exists before the thread starts.
    std::thread _thread;
};
