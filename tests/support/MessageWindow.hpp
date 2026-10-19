#pragma once

#include "platform/Errors.hpp"
#include "platform/Threads.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

// Must be destroyed on the thread that made it.
class MessageWindow {
  public:
    MessageWindow() : _window(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)) {
        if (_window == nullptr) {
            ThrowLastError("CreateWindowExW(message window)");
        }
    }
    ~MessageWindow() {
        DestroyWindow(_window);
    }
    MessageWindow(const MessageWindow&) = delete;
    MessageWindow& operator=(const MessageWindow&) = delete;

    HWND Get() const {
        return _window;
    }

  private:
    HWND _window;
};

struct ChangeTarget {
    explicit ChangeTarget(std::uint32_t message) : signal(window.Get(), message) {}

    MessageWindow window;
    ChangeSignal signal;
};

// Leaves this thread's message queue alone, so a test can read what was posted to it.
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

// Eventually, but answers sent messages (a reader thread's WM_GETICON waits on it) and leaves posted ones queued.
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
