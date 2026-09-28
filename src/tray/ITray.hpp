#pragma once

#include "render/Icon.hpp"

#include <guiddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A mouse button pressed and released over a tray icon.
enum class TrayButton : std::uint8_t {
    Left,
    Right
};

// One notification area icon an app added through Shell_NotifyIcon.
struct TrayIcon {
    std::intptr_t       owner;           // the app's window, which gets the icon's input
    std::uint32_t       id;              // the app's id for the icon, unique per owner
    std::optional<GUID> guid;            // identifies the icon in place of owner and id, when the app gave one
    std::uint32_t       callbackMessage; // posted to owner on input, 0 for none
    std::uint32_t       version;         // NIM_SETVERSION's, which decides how input is posted
    std::wstring        tooltip;         // safe to draw, may hold line breaks
    // Null until the app gives an icon that reads. Kept as the same pointer while the pixels stay the same.
    std::shared_ptr<const IconPixels> pixels;
    bool                              hidden; // NIS_HIDDEN

    bool                              operator==(const TrayIcon&) const = default;
};

// The notification area icons apps add, read from the UI thread.
class ITray {
  public:
    virtual ~ITray() = default;

    // Every icon, in the order apps added them, hidden ones included.
    virtual std::vector<TrayIcon> Icons() const = 0;

    // Tells the icon's app that button was clicked on it, as the taskbar would, so it can open its window or menu.
    virtual void Press(const TrayIcon& icon, TrayButton button) = 0;
};
