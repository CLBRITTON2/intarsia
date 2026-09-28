#pragma once

#include "render/Icon.hpp"
#include "tray/ITray.hpp"

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// COPYDATASTRUCT::dwData of a Shell_NotifyIcon call to the taskbar window (Shell_TrayWnd), and of an SHAppBarMessage
// call. Shell_NotifyIconGetRect sends 3.
inline constexpr ULONG_PTR kNotifyIconCopyData = 1;
inline constexpr ULONG_PTR kAppBarCopyData     = 0;

// NOTIFYICONDATAW as shell32 sends it, whatever size the app passed: every handle is 32 bits wide, so 32-bit and
// 64-bit apps send the same layout.
struct NotifyIconData32 {
    std::uint32_t            size;
    std::uint32_t            window;
    std::uint32_t            id;
    std::uint32_t            flags;
    std::uint32_t            callbackMessage;
    std::uint32_t            icon;
    std::array<wchar_t, 128> tip;
    std::uint32_t            state;
    std::uint32_t            stateMask;
    std::array<wchar_t, 256> info;
    std::uint32_t            version; // a union with the balloon timeout
    std::array<wchar_t, 64>  infoTitle;
    std::uint32_t            infoFlags;
    GUID                     guid;
    std::uint32_t            balloonIcon;
};

// The WM_COPYDATA payload of a Shell_NotifyIcon call. Windows 11 sends more bytes after it, which nothing here reads.
struct TrayNotifyData {
    std::uint32_t    signature; // kTrayNotifySignature
    std::uint32_t    command;   // NIM_ADD, NIM_MODIFY, ...
    NotifyIconData32 data;
};

inline constexpr std::uint32_t kTrayNotifySignature = 0x34753423;

// APPBARDATA as shell32 sends it, with the window 32 bits wide, and as the taskbar writes a result back.
struct AppBarData32 {
    std::uint32_t size; // sizeof(AppBarData32)
    std::uint32_t window;
    std::uint32_t callbackMessage;
    std::uint32_t edge;
    RECT          rect;
    std::int64_t  lParam;
};

// The WM_COPYDATA payload of an SHAppBarMessage call, as shell32 sends it on 64-bit Windows (no public layout).
struct AppBarCommand {
    AppBarData32  data;
    std::uint32_t message; // ABM_*
    std::uint32_t reserved;
    // Where the taskbar writes the AppBarData32 of ABM_QUERYPOS and ABM_SETPOS, zero for the other messages: a
    // SHAllocShared handle that resultProcess holds.
    std::uint64_t result;
    std::uint32_t resultProcess;
    std::uint32_t reserved2;
};

// A payload that is not a whole message of its kind.
class TrayMessageError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// One Shell_NotifyIcon call, with handles widened as Windows sign-extends them.
struct NotifyIconMessage {
    std::uint32_t command;
    std::intptr_t owner;
    std::uint32_t id;
    std::uint32_t flags; // NIF_*: which fields below the app set
    std::uint32_t callbackMessage;
    std::intptr_t icon; // an HICON, valid only while the sender waits on the message
    std::wstring  tip;  // as the app wrote it, up to its first null
    std::uint32_t state;
    std::uint32_t stateMask;
    std::uint32_t version;
    GUID          guid;
};

// Throws TrayMessageError when payload is too short or does not start with the signature.
NotifyIconMessage ParseNotifyIconMessage(std::span<const std::byte> payload);

// Throws TrayMessageError when payload is shorter than an AppBarCommand.
AppBarCommand ParseAppBarCommand(std::span<const std::byte> payload);

// payload with its result handle and the process that holds it replaced, and any bytes past the command kept. Throws
// TrayMessageError when payload is shorter than an AppBarCommand.
std::vector<std::byte> WithAppBarResult(std::span<const std::byte> payload, std::uint64_t result, std::uint32_t resultProcess);

// At most this many icons: any process can add them, and each holds its pixels.
inline constexpr size_t kMaxTrayIcons = 128;

// The icons after message, or nullopt when the taskbar would refuse it: an icon it does not have, a full tray, an
// unknown command. pixels is the message's icon read on arrival, null when it has none or it did not read. Adding an
// icon that exists updates it, since apps add their icons again whenever a taskbar announces itself.
std::optional<std::vector<TrayIcon>> ApplyNotifyIcon(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::shared_ptr<const IconPixels> pixels);

// A tooltip as an app wrote it, made safe to draw: line breaks stay, a carriage return goes, and every other unsafe
// character or lone surrogate becomes U+FFFD.
std::wstring DrawableTooltip(std::wstring_view tip);

// A message the taskbar posts to an icon's owner.
struct TrayInputMessage {
    std::uint32_t message;
    WPARAM        wParam;
    LPARAM        lParam;

    bool          operator==(const TrayInputMessage&) const = default;
};

// The events the taskbar tells an icon of a version for a click: the button's down and up, then from version 3 on
// NIN_SELECT for a left click or WM_CONTEXTMENU for a right one.
std::vector<std::uint32_t> ClickEvents(TrayButton button, std::uint32_t version);

// The events for the second click of a double click: the button's double click and up. Version 3 and on tell no
// NIN_SELECT again, since the first click already did.
std::vector<std::uint32_t> DoubleClickEvents(TrayButton button);

// events posted to the icon's callback with the cursor at cursor, in the form its version asks for. None when the
// callback is below WM_USER: apps define their own, and a system message there, such as WM_CLOSE, would let any
// process that adds an icon have the bar send it to any window.
std::vector<TrayInputMessage> TrayInputMessages(const TrayIcon& icon, const std::vector<std::uint32_t>& events, POINT cursor);

// One click on an icon, as double click detection compares them.
struct TrayClick {
    std::intptr_t                         owner;
    std::uint32_t                         id;
    TrayButton                            button;
    POINT                                 cursor;
    std::chrono::steady_clock::time_point time;
};

// Whether click is the second of a double click after previous: the same button on the same icon, within doubleTime
// and within the double click rectangle (area, centered on the first click), as Windows tells double clicks apart.
bool IsDoubleClick(const std::optional<TrayClick>& previous, const TrayClick& click, std::chrono::milliseconds doubleTime, SIZE area);
