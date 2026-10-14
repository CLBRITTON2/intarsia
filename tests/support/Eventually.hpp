#pragma once

#include <windows.h>

#include <chrono>
#include <functional>
#include <thread>

// Polls condition until it holds or limit passes. Leaves this thread's message queue alone, so a test can read what
// was posted to it.
inline bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return condition();
}

// Eventually, answering the messages other threads send this one meanwhile (a reader thread's WM_GETICON waits on
// it). Posted messages stay queued, so condition can read them.
inline bool AnsweringUntil(const std::function<bool()>& condition, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_SENDMESSAGE);
        MSG message{};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    return condition();
}
