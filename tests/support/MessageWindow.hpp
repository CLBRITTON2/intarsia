#pragma once

#include "platform/Errors.hpp"

#include <windows.h>

// A message-only window for code under test to post to, destroyed with the object on the thread that made it.
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
    MessageWindow(const MessageWindow&)            = delete;
    MessageWindow& operator=(const MessageWindow&) = delete;

    HWND           Get() const {
        return _window;
    }

  private:
    HWND _window;
};
