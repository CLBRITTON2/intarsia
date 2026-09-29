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

// COPYDATASTRUCT::dwData of a Shell_NotifyIcon call to the taskbar window (Shell_TrayWnd), and of a
// Shell_NotifyIconGetRect call. An SHAppBarMessage call sends kAppBarCopyData.
inline constexpr ULONG_PTR kNotifyIconCopyData = 1;
inline constexpr ULONG_PTR kIconRectCopyData   = 3;

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

// The part of an icon's rect a Shell_NotifyIconGetRect message asks for. shell32 asks for the size first and gives up
// when the answer is 0, then for the top left corner.
enum class IconRectPart : std::uint8_t {
    Position = 1,
    Size     = 2
};

// The WM_COPYDATA payload of a Shell_NotifyIconGetRect call: a NOTIFYICONIDENTIFIER with a 32-bit window.
struct NotifyIconIdentifier32 {
    std::uint32_t signature; // kTrayNotifySignature
    std::uint32_t part;      // IconRectPart
    std::uint32_t size;      // the app's NOTIFYICONIDENTIFIER::cbSize
    std::uint32_t padding;
    std::uint32_t window;
    std::uint32_t id;
    GUID          guid;
};

// One Shell_NotifyIconGetRect message. The icon is its guid when that is not all zero, else its owner and id.
struct IconRectQuery {
    IconRectPart  part;
    std::intptr_t owner;
    std::uint32_t id;
    GUID          guid;
};

// Throws TrayMessageError when payload is too short, does not start with the signature or asks for an unknown part.
IconRectQuery ParseIconRectQuery(std::span<const std::byte> payload);

// Where the queried icon is: of every rect the views show it at, the one nearest cursor, since the app asks about the
// icon the user just clicked or hovered. Nullopt when it is not a tray icon or no view shows it uncovered.
std::optional<RECT> QueriedIconRect(const IconRectQuery& query, const std::vector<TrayIcon>& icons, const std::vector<TrayIconRect>& rects, POINT cursor);

// The answer to a Shell_NotifyIconGetRect message: the part of rect as two signed 16-bit halves, x low.
LRESULT IconRectAnswer(IconRectPart part, const RECT& rect);

// At most this many icons: any process can add them, and each holds its pixels.
inline constexpr size_t kMaxTrayIcons = 128;

// The icons after message, or nullopt when the taskbar would refuse it: an icon it does not have, a full tray, an
// unknown command, a GUID another executable gave. ownerImage is the executable of the message's owner, read only for a
// message with a GUID. pixels is the message's icon read on arrival, null when it has none or it did not read. Adding an
// icon that exists updates it, since apps add their icons again whenever a taskbar announces itself.
std::optional<std::vector<TrayIcon>> ApplyNotifyIcon(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::wstring_view ownerImage,
                                                     std::shared_ptr<const IconPixels> pixels);

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
