#pragma once

#include "render/Icon.hpp"

#include <guiddef.h>
#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A mouse button pressed and released over a tray icon.
enum class TrayButton : std::uint8_t {
    Left,
    Right,
    Middle
};

// One notification area icon an app added through Shell_NotifyIcon.
struct TrayIcon {
    std::intptr_t       owner;           // the app's window, which gets the icon's input
    std::uint32_t       id;              // the app's id for the icon, unique per owner
    std::optional<GUID> guid;            // identifies the icon in place of owner and id, when the app gave one
    std::wstring        ownerImage;      // the executable that gave guid, the only one that may use it after
    std::uint32_t       callbackMessage; // posted to owner on input, 0 for none
    std::uint32_t       version;         // NIM_SETVERSION's, which decides how input is posted
    std::wstring        tooltip;         // safe to draw, may hold line breaks
    bool                standardTooltip; // NIF_SHOWTIP on the app's last add or modify
    // Null until the app gives an icon that reads. Kept as the same pointer while the pixels stay the same.
    std::shared_ptr<const IconPixels> pixels;
    bool                              hidden; // NIS_HIDDEN

    bool                              operator==(const TrayIcon&) const = default;
};

// Whether the icon's app shows its own popup on hover in place of the tooltip: from version 4, unless it asked for the
// standard tooltip.
inline bool DrawsOwnPopup(const TrayIcon& icon) {
    constexpr std::uint32_t kVersion4 = 4; // NOTIFYICON_VERSION_4
    return icon.version >= kVersion4 && !icon.standardTooltip;
}

// Where one bar shows an icon, fully uncovered.
struct TrayIconRect {
    std::intptr_t owner;
    std::uint32_t id;
    RECT          rect; // screen pixels
};

// The notification area icons apps add, read from the UI thread.
class ITray {
  public:
    virtual ~ITray() = default;

    // Every icon, in the order apps added them, hidden ones included.
    virtual std::vector<TrayIcon> Icons() const = 0;

    // Tells the icon's app that button was clicked on it, as the taskbar would, so it can open its window or menu. A
    // second click soon enough and close enough to the first reaches it as a double click.
    virtual void Press(const TrayIcon& icon, TrayButton button) = 0;

    // Tells the icon's app that the cursor moved over it, as the taskbar does. A move that leaves the cursor where the
    // last one did, as the bar reports after every frame, is not told again.
    virtual void Move(const TrayIcon& icon) = 0;

    // Tells an app that draws its own popup (DrawsOwnPopup) that the cursor came to rest on its icon, and that it left.
    virtual void OpenPopup(const TrayIcon& icon)  = 0;
    virtual void ClosePopup(const TrayIcon& icon) = 0;

    // Where view, one tray module, shows its icons now, replacing what it said before, so an app asking where its icon
    // is (Shell_NotifyIconGetRect) learns its place on a bar. A view reports none once it is gone.
    virtual void ReportIconRects(const void* view, std::vector<TrayIconRect> rects) = 0;
};
