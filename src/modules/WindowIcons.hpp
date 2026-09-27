#pragma once

#include "platform/ChangeSignal.hpp"
#include "platform/WorkQueue.hpp"

#include <windows.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// An icon's pixels, premultiplied BGRA with no row padding, ready for ID2D1DeviceContext::CreateBitmap.
struct IconPixels {
    std::uint32_t             width;
    std::uint32_t             height;
    std::vector<std::uint8_t> pbgra;
};

// The icon a window shows in the taskbar: its own (WM_GETICON, large first since the bar scales it down), then its
// class's. Null when it has neither. Borrowed: the window's process owns it. A window that does not answer costs up
// to timeoutMs per ask, so never call this on the UI thread.
HICON WindowIcon(HWND window, std::uint32_t timeoutMs);

// The icon's pixels. Throws Win32Error when WIC cannot read it.
IconPixels IconToPixels(HICON icon);

// Window icons for every bar, read on a worker thread and kept until Retain drops them. Notifies changes whenever an icon
// arrives.
class WindowIcons {
  public:
    explicit WindowIcons(ChangeSignal& changes);

    // The window's icon, or null while it is being read or when the window has none. The first ask queues the read.
    std::shared_ptr<const IconPixels> For(std::intptr_t window);

    // Forgets every window not in windows, so icons of closed windows do not pile up.
    void Retain(const std::vector<std::intptr_t>& windows);

  private:
    void          Read(std::intptr_t window);

    ChangeSignal& _changes;
    std::mutex    _mutex; // guards _icons
    // Null while the read is queued or when the window has no icon.
    std::map<std::intptr_t, std::shared_ptr<const IconPixels>> _icons;
    // Declared last so every member above exists before its thread starts, and it stops before they go.
    WorkQueue<std::intptr_t> _reader;
};
