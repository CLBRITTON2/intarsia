#pragma once

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
