#pragma once

#include "platform/Handle.hpp"
#include "platform/Threads.hpp"
#include "render/Surface.hpp"

#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

// Posts frameMessage to target once per vertical blank of output while started, so animations draw at that monitor's
// refresh rather than a timer's (DwmFlush would follow one composition clock for every monitor, the primary's). As
// ChangeSignal, it posts at most one message until the UI thread acknowledges it. A failure that stops its thread posts
// failedMessage.
class FrameClock {
  public:
    FrameClock(HWND target, std::uint32_t frameMessage, std::uint32_t failedMessage, Microsoft::WRL::ComPtr<IDXGIOutput> output);
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    void Start();
    void Stop();
    // The UI thread, once it has drawn the frame, so the next frame posts again at the following vertical blank.
    void Acknowledge();
    // On failedMessage, rethrows the error that stopped the thread.
    void RethrowFailure();
    // When the latest vertical blank the clock waited for came, or the clock's epoch before the first.
    std::chrono::steady_clock::time_point LastVBlank() const;

  private:
    void Run(HANDLE stopEvent);

    HWND _target;
    std::uint32_t _frameMessage;
    Microsoft::WRL::ComPtr<IDXGIOutput> _output;
    UniqueHandle _runningEvent; // set while started
    std::atomic<bool> _pending = false;
    std::atomic<std::chrono::steady_clock::time_point> _lastVBlank;
    ThreadFailure _failure;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

// How one animation's frames went on one bar, logged when it ends so its pacing can be read from the log. Frames the
// display never showed and refreshes that showed a frame again are counted from the swap chain's statistics, which the
// presents' own timing cannot show.
struct FrameStats {
    std::uint32_t frames;
    std::chrono::steady_clock::time_point first;
    std::chrono::steady_clock::time_point last;
    std::chrono::steady_clock::duration longestInterval;
    std::chrono::steady_clock::duration longestPresent;
    std::uint32_t droppedFrames;
    std::uint32_t repeatedRefreshes;
    std::uint32_t firstPresent;            // the swap chain's number for the animation's first present
    std::optional<ShownPresent> lastShown; // null until the swap chain has statistics
};

// stats with one more frame, the swap chain's present `presented`, made at `at` by a present that took `present`, once
// the display has shown `shown`. No stats starts them.
FrameStats WithFrame(const std::optional<FrameStats>& stats, std::chrono::steady_clock::time_point at, std::chrono::steady_clock::duration present, std::uint32_t presented,
                     const std::optional<ShownPresent>& shown);

// Writes stats to the log as the animation_frames event of the bar on monitorRect.
void LogAnimationFrames(const FrameStats& stats, const RECT& monitorRect);
