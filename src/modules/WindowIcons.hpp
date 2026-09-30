#pragma once

#include "platform/ChangeSignal.hpp"
#include "platform/WorkQueue.hpp"
#include "render/Icon.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

// The icon a window shows in the taskbar: its own (WM_GETICON, large first since the bar scales it down), then its
// class's. Null when it has neither. Borrowed: the window's process owns it. A window that does not answer costs up
// to timeoutMs per ask, so never call this on the UI thread.
HICON WindowIcon(HWND window, std::uint32_t timeoutMs);

// Window icons for every bar, read on a worker thread and kept while the window exists, so a workspace switch back finds
// them. Notifies changes whenever a read ends.
class WindowIcons {
  public:
    // A window that showed no icon is read again once retryAfter has passed: apps often set theirs after the window
    // appears.
    WindowIcons(ChangeSignal& changes, std::chrono::milliseconds retryAfter);

    // The window's icon, or null while it is being read or when the window has none. The first ask queues the read, and
    // an ask once a read without an icon is retryAfter old queues another.
    std::shared_ptr<const IconPixels> For(std::intptr_t window);

    // Forgets every window that no longer exists, so icons of closed windows do not pile up.
    void ForgetClosed();

  private:
    struct Entry {
        std::shared_ptr<const IconPixels> pixels;
        // Set once a read ends without an icon. Unset while a read is queued, or for good once one found an icon.
        std::optional<std::chrono::steady_clock::time_point> retryAt;
    };

    void                           Read(std::intptr_t window);

    ChangeSignal&                  _changes;
    std::chrono::milliseconds      _retryAfter;
    std::mutex                     _mutex; // guards _icons
    std::map<std::intptr_t, Entry> _icons;
    // Declared last so every member above exists before its thread starts, and it stops before they go.
    WorkQueue<std::intptr_t> _reader;
};
