#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ItemRow.hpp"
#include "platform/Threads.hpp"
#include "render/Icon.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

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

    // Null imaging reads nothing: the reader thread could not set up WIC.
    void                           Read(IWICImagingFactory* imaging, std::intptr_t window);

    ChangeSignal&                  _changes;
    std::chrono::milliseconds      _retryAfter;
    std::mutex                     _mutex; // guards _icons
    std::map<std::intptr_t, Entry> _icons;
    // Declared last so every member above exists before its thread starts, and it stops before they go.
    WorkQueue<std::intptr_t> _reader;
};

// One icon per window on the displayed workspace of this bar's monitor, in the window manager's order, each an item in
// the module's item style, item focused for the focused one. The icon is the module's icon size. A click
// focuses the window under it.
class WindowList final : public IModule {
  public:
    WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor, ModuleStyle style);

    bool        Layout(const RenderContext& render) override;
    D2D1_SIZE_F Size() const override;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    void        Click(D2D1_POINT_2F point) override;
    bool        Hover(std::optional<D2D1_POINT_2F> point) override;
    // 0 while an item fades, so the bar draws every display frame until the fades end.
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    // None: the module has no config table, and a left click focuses the window under it.
    const ModuleActions& Actions() const override;

  private:
    HoveredItem _hovered;
    ItemFades   _fades;
    // One shown window. The bitmap is made from pixels on this bar's device, and is null while the icon is unknown.
    struct Slot {
        ManagedWindow                       window;
        std::shared_ptr<const IconPixels>   pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    const ItemStyle& StyleOf(const Slot& slot) const;
    // A bitmap already made from the same window's same pixels is kept: making one costs an upload to the GPU.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> BitmapFor(const RenderContext& render, const ManagedWindow& window, const std::shared_ptr<const IconPixels>& pixels) const;
    // Layout's work before the fades follow it.
    bool                          LayOutItems(const RenderContext& render);
    std::vector<const ItemStyle*> ItemStyles() const;

    IWindowManager&               _windowManager;
    WindowIcons&                  _icons;
    std::intptr_t                 _monitor;
    ModuleStyle                   _style;
    std::vector<Slot>             _slots;
    ItemRow                       _row     = EmptyItemRow();
    bool                          _laidOut = false;
};
