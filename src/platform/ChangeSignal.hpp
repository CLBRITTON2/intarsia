#pragma once

#include <windows.h>

#include <atomic>
#include <string_view>

// Tells the UI thread that data a module shows has changed. Posts at most one message until the UI thread acknowledges
// it: a source that changes faster than the UI thread keeps up would otherwise fill its queue, and GetMessage returns
// a posted WM_QUIT only once no other posted message is waiting, so the bar could never quit.
class ChangeSignal {
  public:
    ChangeSignal(HWND target, UINT message);
    ChangeSignal(const ChangeSignal&)            = delete;
    ChangeSignal& operator=(const ChangeSignal&) = delete;

    // Any thread. component names the source in the log line a failed post writes.
    void Notify(std::string_view component);

    // The UI thread, as it handles the message and before it reads the sources, so a change made after that posts again.
    void Acknowledge();

  private:
    HWND              _target;
    UINT              _message;
    std::atomic<bool> _pending = false;
};
