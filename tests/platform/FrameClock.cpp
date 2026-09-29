#include "platform/FrameClock.hpp"
#include "render/Graphics.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

namespace {

    constexpr std::uint32_t kFrame  = WM_APP + 1;
    constexpr std::uint32_t kFailed = WM_APP + 2;

    // Frames that arrive within limit, acknowledging each as the UI thread does.
    std::int32_t CountFrames(FrameClock& clock, HWND target, std::chrono::milliseconds limit) {
        std::int32_t frames   = 0;
        const auto   deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
            MSG message{};
            while (PeekMessageW(&message, target, 0, 0, PM_REMOVE)) {
                EXPECT_NE(message.message, kFailed);
                if (message.message == kFrame) {
                    ++frames;
                    clock.Acknowledge();
                }
            }
        }
        return frames;
    }

} // namespace

// At 60 to 360 Hz half a second is 30 to 180 frames. A clock that did not wait for the refresh would post thousands.
TEST(FrameClock, PostsOncePerDisplayFrameWhileStarted) {
    HWND target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    {
        FrameClock clock(target, kFrame, kFailed, OutputForMonitor(MonitorFromPoint(POINT{.x = 0, .y = 0}, MONITOR_DEFAULTTOPRIMARY)).output);
        EXPECT_EQ(CountFrames(clock, target, std::chrono::milliseconds(200)), 0);

        clock.Start();
        const std::int32_t frames = CountFrames(clock, target, std::chrono::milliseconds(500));
        EXPECT_GE(frames, 15);
        EXPECT_LE(frames, 200);

        clock.Stop();
        CountFrames(clock, target, std::chrono::milliseconds(50));
        EXPECT_EQ(CountFrames(clock, target, std::chrono::milliseconds(200)), 0);
    }
    DestroyWindow(target);
}
