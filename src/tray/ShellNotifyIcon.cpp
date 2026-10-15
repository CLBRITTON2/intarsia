#include "tray/ShellNotifyIcon.hpp"

#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"
#include "platform/Threads.hpp"
#include "platform/Win32Window.hpp"
#include "render/Icon.hpp"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    static_assert(sizeof(TrayNotifyData) == 964, "TrayNotifyData must match shell32's layout");
    static_assert(sizeof(NotifyIconIdentifier32) == 40, "NotifyIconIdentifier32 must match shell32's layout");

    // Windows keeps handles to 32 significant bits and sign-extends them on 64-bit.
    std::intptr_t WidenHandle(std::uint32_t handle) {
        return static_cast<std::intptr_t>(static_cast<std::int32_t>(handle));
    }

    std::wstring UpToNull(const std::array<wchar_t, 128>& text) {
        return {text.begin(), std::ranges::find(text, L'\0')};
    }

    // An icon with a GUID is found by it alone, one without by its owner and id.
    auto FindIcon(std::vector<TrayIcon>& icons, const NotifyIconMessage& message) {
        if ((message.flags & NIF_GUID) != 0) {
            return std::ranges::find_if(icons, [&message](const TrayIcon& icon) { return icon.guid == message.guid; });
        }
        return std::ranges::find_if(icons, [&message](const TrayIcon& icon) { return icon.owner == message.owner && icon.id == message.id; });
    }

    // The fields the message's flags say it sets. The same pixels as before keep their pointer, so an app that sets its
    // icon again unchanged costs no upload or redraw.
    TrayIcon Updated(TrayIcon icon, const NotifyIconMessage& message, std::wstring_view ownerImage, std::shared_ptr<const IconPixels> pixels) {
        icon.owner = message.owner;
        icon.id    = message.id;
        if ((message.flags & NIF_GUID) != 0) {
            icon.guid       = message.guid;
            icon.ownerImage = ownerImage;
        }
        if ((message.flags & NIF_MESSAGE) != 0) {
            icon.callbackMessage = message.callbackMessage;
        }
        if ((message.flags & NIF_TIP) != 0) {
            icon.tooltip = DrawableTooltip(message.tip);
        }
        // An app that swaps only its icon may leave NIF_SHOWTIP out without meaning to drop the tooltip.
        if (message.command == NIM_ADD || (message.flags & NIF_TIP) != 0) {
            icon.standardTooltip = (message.flags & NIF_SHOWTIP) != 0;
        }
        if ((message.flags & NIF_ICON) != 0) {
            const bool same = icon.pixels != nullptr && pixels != nullptr && *icon.pixels == *pixels;
            if (!same) {
                icon.pixels = std::move(pixels);
            }
        }
        if ((message.flags & NIF_STATE) != 0 && (message.stateMask & NIS_HIDDEN) != 0) {
            icon.hidden = (message.state & NIS_HIDDEN) != 0;
        }
        return icon;
    }

    WORD Signed16(std::int32_t value) {
        return static_cast<WORD>(static_cast<std::int16_t>(value));
    }

    WPARAM CursorWParam(POINT cursor) {
        return MAKEWPARAM(Signed16(cursor.x), Signed16(cursor.y));
    }

    // 0 inside the rect.
    std::int64_t SquaredDistanceToRect(const RECT& rect, POINT point) {
        const std::int64_t gapX = std::max({static_cast<std::int64_t>(rect.left) - point.x, std::int64_t{0}, static_cast<std::int64_t>(point.x) - rect.right});
        const std::int64_t gapY = std::max({static_cast<std::int64_t>(rect.top) - point.y, std::int64_t{0}, static_cast<std::int64_t>(point.y) - rect.bottom});
        return (gapX * gapX) + (gapY * gapY);
    }

    // Often enough that an app's message rarely goes to Explorer alone after another topmost window rose above ours.
    constexpr UINT_PTR      kFirstTimer   = 1;
    constexpr std::uint32_t kFirstEveryMs = 100;
    constexpr UINT_PTR      kSweepTimer   = 2;
    constexpr std::uint32_t kSweepEveryMs = 5000;
    // The sender waits on the forward, so a hung Explorer must not hold it for long.
    constexpr std::uint32_t kForwardTimeoutMs = 2000;

    // Null while the cursor cannot be read, which is so on another desktop (the lock screen, a UAC prompt): the bar
    // still redraws and hovers there, and an app told of input it cannot see would be wrong anyway. Logged once per
    // outage.
    std::optional<POINT> CursorPosition(FailureLog& log) {
        POINT cursor{};
        if (!GetCursorPos(&cursor)) {
            log.Failed("cursor_unavailable", Win32Error("GetCursorPos", HRESULT_FROM_WIN32(GetLastError())));
            return std::nullopt;
        }
        log.Succeeded();
        return cursor;
    }

    HWND CreateTrayWindow(const std::wstring& windowClass, WNDPROC procedure, void* owner) {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        RegisterWindowClass(instance, windowClass.c_str(), procedure, nullptr, "tray");
        try {
            return CreatePopupWindow(instance, windowClass.c_str(), L"", WS_EX_TOOLWINDOW | WS_EX_TOPMOST, owner, "tray");
        } catch (const Win32Error&) {
            UnregisterClassW(windowClass.c_str(), instance);
            throw;
        }
    }

    // Throws Win32Error, as for a window already gone or a process of another user.
    std::wstring WindowImage(HWND window) {
        DWORD processId = 0;
        if (GetWindowThreadProcessId(window, &processId) == 0) {
            ThrowLastError("GetWindowThreadProcessId(tray icon owner)");
        }
        const UniqueHandle process = OwnHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId), "OpenProcess(tray icon owner)");
        return ProcessImage(process.get()).wstring();
    }

} // namespace

NotifyIconMessage ParseNotifyIconMessage(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(TrayNotifyData)) {
        throw TrayMessageError(std::format("a notify icon message is at least {} bytes, got {}", sizeof(TrayNotifyData), payload.size()));
    }
    TrayNotifyData raw{};
    std::memcpy(&raw, payload.data(), sizeof(raw));
    if (raw.signature != kTrayNotifySignature) {
        throw TrayMessageError(std::format("a notify icon message starts with 0x{:08X}, got 0x{:08X}", kTrayNotifySignature, raw.signature));
    }
    return NotifyIconMessage{
        .command         = raw.command,
        .owner           = WidenHandle(raw.data.window),
        .id              = raw.data.id,
        .flags           = raw.data.flags,
        .callbackMessage = raw.data.callbackMessage,
        .icon            = WidenHandle(raw.data.icon),
        .tip             = UpToNull(raw.data.tip),
        .state           = raw.data.state,
        .stateMask       = raw.data.stateMask,
        .version         = raw.data.version,
        .guid            = raw.data.guid,
    };
}

IconRectQuery ParseIconRectQuery(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(NotifyIconIdentifier32)) {
        throw TrayMessageError(std::format("an icon rect query is at least {} bytes, got {}", sizeof(NotifyIconIdentifier32), payload.size()));
    }
    NotifyIconIdentifier32 raw{};
    std::memcpy(&raw, payload.data(), sizeof(raw));
    if (raw.signature != kTrayNotifySignature) {
        throw TrayMessageError(std::format("an icon rect query starts with 0x{:08X}, got 0x{:08X}", kTrayNotifySignature, raw.signature));
    }
    // Checked before the cast, which would keep only the low byte.
    if (raw.part != static_cast<std::uint32_t>(IconRectPart::Position) && raw.part != static_cast<std::uint32_t>(IconRectPart::Size)) {
        throw TrayMessageError(std::format("an icon rect query asks for part 1 or 2, got {}", raw.part));
    }
    return IconRectQuery{
        .part  = static_cast<IconRectPart>(raw.part),
        .owner = WidenHandle(raw.window),
        .id    = raw.id,
        .guid  = raw.guid,
    };
}

std::optional<RECT> QueriedIconRect(const IconRectQuery& query, const std::vector<TrayIcon>& icons, const std::vector<TrayIconRect>& rects, POINT cursor) {
    const bool byGuid = query.guid != GUID{};
    const auto icon   = std::ranges::find_if(icons, [&query, byGuid](const TrayIcon& candidate) {
        if (byGuid) {
            return candidate.guid == query.guid;
        }
        return candidate.owner == query.owner && candidate.id == query.id;
    });
    if (icon == icons.end()) {
        return std::nullopt;
    }
    std::optional<RECT> nearest;
    for (const TrayIconRect& shown : rects) {
        if (shown.owner == icon->owner && shown.id == icon->id && (!nearest.has_value() || SquaredDistanceToRect(shown.rect, cursor) < SquaredDistanceToRect(*nearest, cursor))) {
            nearest = shown.rect;
        }
    }
    return nearest;
}

LRESULT IconRectAnswer(IconRectPart part, const RECT& rect) {
    switch (part) {
        case IconRectPart::Position: return MAKELONG(Signed16(rect.left), Signed16(rect.top));
        case IconRectPart::Size: return MAKELONG(Signed16(rect.right - rect.left), Signed16(rect.bottom - rect.top));
    }
    throw std::invalid_argument(std::format("unknown icon rect part {}", static_cast<std::uint32_t>(part)));
}

std::optional<std::vector<TrayIcon>> ApplyNotifyIcon(const std::vector<TrayIcon>& icons, const NotifyIconMessage& message, std::wstring_view ownerImage,
                                                     std::shared_ptr<const IconPixels> pixels) {
    std::vector<TrayIcon> updated = icons;
    const auto            icon    = FindIcon(updated, message);
    const bool            found   = icon != updated.end();
    // Any process can name any GUID, so without this one could take another app's icon and the clicks on it.
    if (found && (message.flags & NIF_GUID) != 0 && !SameFileName(icon->ownerImage, ownerImage)) {
        return std::nullopt;
    }
    if (!found && message.command != NIM_ADD) {
        return std::nullopt;
    }
    switch (message.command) {
        case NIM_ADD:
            if (found) {
                *icon = Updated(*icon, message, ownerImage, std::move(pixels));
                return updated;
            }
            if (updated.size() >= kMaxTrayIcons) {
                return std::nullopt;
            }
            updated.push_back(Updated(
                TrayIcon{
                    .owner           = message.owner,
                    .id              = message.id,
                    .guid            = std::nullopt,
                    .ownerImage      = {},
                    .callbackMessage = 0,
                    .version         = 0,
                    .tooltip         = {},
                    .standardTooltip = false,
                    .pixels          = nullptr,
                    .hidden          = false,
                },
                message, ownerImage, std::move(pixels)));
            return updated;
        case NIM_MODIFY: *icon = Updated(*icon, message, ownerImage, std::move(pixels)); return updated;
        case NIM_DELETE: updated.erase(icon); return updated;
        case NIM_SETVERSION: icon->version = message.version; return updated;
        case NIM_SETFOCUS: return updated;
        default: return std::nullopt;
    }
}

std::wstring DrawableTooltip(std::wstring_view tip) {
    std::wstring kept;
    kept.reserve(tip.size());
    std::ranges::copy_if(tip, std::back_inserter(kept), [](wchar_t unit) { return unit != L'\r'; });
    std::ranges::replace_if(kept, [](wchar_t unit) { return unit != L'\n' && IsUnsafeCharacter(unit); }, kReplacementCharacter);
    return ReplaceLoneSurrogates(kept);
}

std::vector<std::uint32_t> ClickEvents(TrayButton button, std::uint32_t version) {
    const bool notifies = version >= NOTIFYICON_VERSION;
    switch (button) {
        case TrayButton::Left:
            if (notifies) {
                return {WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT};
            }
            return {WM_LBUTTONDOWN, WM_LBUTTONUP};
        case TrayButton::Right:
            if (notifies) {
                return {WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU};
            }
            return {WM_RBUTTONDOWN, WM_RBUTTONUP};
        case TrayButton::Middle: return {WM_MBUTTONDOWN, WM_MBUTTONUP};
    }
    throw std::invalid_argument(std::format("unknown tray button {}", static_cast<std::int32_t>(button)));
}

std::vector<std::uint32_t> DoubleClickEvents(TrayButton button) {
    switch (button) {
        case TrayButton::Left: return {WM_LBUTTONDBLCLK, WM_LBUTTONUP};
        case TrayButton::Right: return {WM_RBUTTONDBLCLK, WM_RBUTTONUP};
        case TrayButton::Middle: return {WM_MBUTTONDBLCLK, WM_MBUTTONUP};
    }
    throw std::invalid_argument(std::format("unknown tray button {}", static_cast<std::int32_t>(button)));
}

std::vector<TrayInputMessage> TrayInputMessages(const TrayIcon& icon, const std::vector<std::uint32_t>& events, POINT cursor) {
    if (icon.callbackMessage < WM_USER) {
        return {};
    }
    std::vector<TrayInputMessage> messages;
    messages.reserve(events.size());
    for (const std::uint32_t event : events) {
        // Version 4 carries the cursor in wParam and the event and id in lParam, earlier versions the id and the event.
        if (icon.version >= NOTIFYICON_VERSION_4) {
            messages.push_back(TrayInputMessage{
                .message = icon.callbackMessage,
                .wParam  = CursorWParam(cursor),
                .lParam  = MAKELPARAM(static_cast<WORD>(event), static_cast<WORD>(icon.id)),
            });
        } else {
            messages.push_back(TrayInputMessage{
                .message = icon.callbackMessage,
                .wParam  = icon.id,
                .lParam  = static_cast<LPARAM>(event),
            });
        }
    }
    return messages;
}

bool IsDoubleClick(const std::optional<TrayClick>& previous, const TrayClick& click, std::chrono::milliseconds doubleTime, SIZE area) {
    if (!previous.has_value() || previous->owner != click.owner || previous->id != click.id || previous->button != click.button) {
        return false;
    }
    const auto elapsed = click.time - previous->time;
    return elapsed >= std::chrono::steady_clock::duration::zero() && elapsed <= doubleTime && std::abs(click.cursor.x - previous->cursor.x) <= area.cx / 2 &&
        std::abs(click.cursor.y - previous->cursor.y) <= area.cy / 2;
}

ShellNotifyIcon::ShellNotifyIcon(ChangeSignal& changes, std::wstring windowClass, HWND target, std::uint32_t failedMessage) :
    _changes(changes), _windowClass(std::move(windowClass)), _failure(target, failedMessage, "tray"), _stopEvent(ManualResetEvent("tray stop")) {
    std::promise<void> ready;
    std::future<void>  created = ready.get_future();
    _thread                    = std::thread([this, &ready] { Run(ready); });
    try {
        created.get();
    } catch (...) {
        _thread.join();
        throw;
    }
}

ShellNotifyIcon::~ShellNotifyIcon() {
    SetEvent(_stopEvent.get());
    _thread.join();
}

std::vector<TrayIcon> ShellNotifyIcon::Icons() const {
    const std::scoped_lock lock(_mutex);
    return _icons;
}

void ShellNotifyIcon::Press(const TrayIcon& icon, TrayButton button) {
    const std::optional<POINT> position = CursorPosition(_cursorLog);
    if (!position.has_value()) {
        return;
    }
    const POINT     cursor = *position;
    const TrayClick click{
        .owner  = icon.owner,
        .id     = icon.id,
        .button = button,
        .cursor = cursor,
        .time   = std::chrono::steady_clock::now(),
    };
    const bool doubled =
        IsDoubleClick(_lastClick, click, std::chrono::milliseconds(GetDoubleClickTime()), SIZE{.cx = GetSystemMetrics(SM_CXDOUBLECLK), .cy = GetSystemMetrics(SM_CYDOUBLECLK)});
    std::vector<std::uint32_t> events;
    if (doubled) {
        // A third click after a double click starts a new pair, as Windows counts them.
        _lastClick = std::nullopt;
        events     = DoubleClickEvents(button);
    } else {
        _lastClick = click;
        events     = ClickEvents(button, icon.version);
    }
    const std::vector<TrayInputMessage> messages = TrayInputMessages(icon, events, cursor);
    if (messages.empty()) {
        Log(LogLevel::Warning, "tray", "input_refused",
            {{.key = "owner", .value = std::to_string(icon.owner)}, {.key = "callback_message", .value = std::to_string(icon.callbackMessage)}});
        return;
    }
    DWORD processId = 0;
    if (GetWindowThreadProcessId(ToWindow(icon.owner), &processId) == 0) {
        Log(LogLevel::Warning, "tray", "owner_gone", {{.key = "owner", .value = std::to_string(icon.owner)}});
        return;
    }
    // The app brings its menu or window to the foreground, which only the process that took the click may allow.
    if (!AllowSetForegroundWindow(processId)) {
        Log(LogLevel::Warning, "tray", "foreground_refused", {{.key = "process", .value = std::to_string(processId)}, {.key = "error", .value = std::to_string(GetLastError())}});
    }
    const DWORD error = PostInput(icon, messages);
    if (error != ERROR_SUCCESS) {
        Log(LogLevel::Warning, "tray", "input_failed", {{.key = "owner", .value = std::to_string(icon.owner)}, {.key = "error", .value = std::to_string(error)}});
    }
}

void ShellNotifyIcon::Move(const TrayIcon& icon) {
    const std::optional<POINT> cursor = CursorPosition(_cursorLog);
    if (!cursor.has_value() || (_lastMove.has_value() && _lastMove->x == cursor->x && _lastMove->y == cursor->y)) {
        return;
    }
    _lastMove = cursor;
    PostHover(icon, WM_MOUSEMOVE, *cursor);
}

void ShellNotifyIcon::OpenPopup(const TrayIcon& icon) {
    const std::optional<POINT> cursor = CursorPosition(_cursorLog);
    if (cursor.has_value()) {
        PostHover(icon, NIN_POPUPOPEN, *cursor);
    }
}

void ShellNotifyIcon::ClosePopup(const TrayIcon& icon) {
    const std::optional<POINT> cursor = CursorPosition(_cursorLog);
    if (cursor.has_value()) {
        PostHover(icon, NIN_POPUPCLOSE, *cursor);
    }
}

void ShellNotifyIcon::ReportIconRects(const void* view, std::vector<TrayIconRect> rects) {
    const std::scoped_lock lock(_mutex);
    std::erase_if(_views, [view](const ViewRects& reported) { return reported.view == view; });
    if (!rects.empty()) {
        _views.push_back(ViewRects{.view = view, .rects = std::move(rects)});
    }
}

// Logged once per outage: an elevated app would refuse every move across its icon.
void ShellNotifyIcon::PostHover(const TrayIcon& icon, std::uint32_t event, POINT cursor) {
    const DWORD error = PostInput(icon, TrayInputMessages(icon, {event}, cursor));
    if (error != ERROR_SUCCESS) {
        _hoverLog.Failed("hover_failed", Win32Error(std::format("PostMessageW(tray event {})", event), HRESULT_FROM_WIN32(error)));
    } else {
        _hoverLog.Succeeded();
    }
}

DWORD ShellNotifyIcon::PostInput(const TrayIcon& icon, const std::vector<TrayInputMessage>& messages) {
    HWND owner = ToWindow(icon.owner);
    for (const TrayInputMessage& message : messages) {
        if (!PostMessageW(owner, message.message, message.wParam, message.lParam)) {
            return GetLastError();
        }
    }
    return ERROR_SUCCESS;
}

void ShellNotifyIcon::RethrowFailure() {
    _failure.RethrowReported();
}

LRESULT CALLBACK ShellNotifyIcon::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams));
    }
    auto* owner = IntToPointer<ShellNotifyIcon>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (owner == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    // An exception cannot unwind through user32.
    try {
        return owner->HandleMessage(window, message, wParam, lParam);
    } catch (...) {
        owner->Fail(std::current_exception());
        return 0;
    }
}

LRESULT ShellNotifyIcon::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COPYDATA:
            if (lParam == 0) {
                return FALSE;
            }
            return HandleCopyData(ToWindow(static_cast<std::intptr_t>(wParam)), *IntToPointer<const COPYDATASTRUCT>(lParam));
        case WM_TIMER:
            if (wParam == kFirstTimer) {
                KeepFirst();
            } else if (wParam == kSweepTimer) {
                DropIconsOfClosedWindows();
            }
            return 0;
        // taskkill sends WM_CLOSE to every top-level window, and the tray goes with App, not on its own.
        case WM_CLOSE: return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

// Shell_NotifyIcon and Shell_NotifyIconGetRect get the tray's answer, and every other message (appbars) Explorer's. The
// payload is read as bytes only, never as an address: the sender chooses every byte, and the window it names in wParam.
LRESULT ShellNotifyIcon::HandleCopyData(HWND sender, const COPYDATASTRUCT& data) {
    if (data.dwData == kIconRectCopyData) {
        return AnswerIconRect(data);
    }
    if (data.dwData != kNotifyIconCopyData) {
        return Forward(sender, data);
    }
    const bool applied = ApplyMessage(data);
    Forward(sender, data);
    if (applied) {
        return TRUE;
    }
    return FALSE;
}

bool ShellNotifyIcon::ApplyMessage(const COPYDATASTRUCT& data) {
    NotifyIconMessage message{};
    try {
        message = ParseNotifyIconMessage(std::span(static_cast<const std::byte*>(data.lpData), data.cbData));
        _messageLog.Succeeded();
    } catch (const TrayMessageError& error) {
        _messageLog.Failed("malformed_message", error);
        return false;
    }
    std::wstring ownerImage;
    if ((message.flags & NIF_GUID) != 0) {
        try {
            ownerImage = WindowImage(ToWindow(message.owner));
            _ownerLog.Succeeded();
        } catch (const Win32Error& error) {
            _ownerLog.Failed("owner_unreadable", error);
            return false;
        }
    }
    std::shared_ptr<const IconPixels> pixels;
    if ((message.flags & NIF_ICON) != 0 && message.icon != 0) {
        try {
            pixels = std::make_shared<const IconPixels>(IconToPixels(*_imaging.Get(), ToIcon(message.icon)));
            _iconLog.Succeeded();
        } catch (const Win32Error& error) {
            // The icon stays without pixels, and so hidden, until the app sets one that reads.
            _iconLog.Failed("icon_unreadable", error);
        }
    }
    std::optional<std::vector<TrayIcon>> icons = ApplyNotifyIcon(_icons, message, ownerImage, std::move(pixels));
    if (!icons.has_value()) {
        return false;
    }
    Publish(std::move(*icons));
    return true;
}

LRESULT ShellNotifyIcon::AnswerIconRect(const COPYDATASTRUCT& data) {
    IconRectQuery query{};
    try {
        query = ParseIconRectQuery(std::span(static_cast<const std::byte*>(data.lpData), data.cbData));
        _messageLog.Succeeded();
    } catch (const TrayMessageError& error) {
        _messageLog.Failed("malformed_message", error);
        return 0;
    }
    const std::optional<POINT> cursor = CursorPosition(_rectLog);
    if (!cursor.has_value()) {
        return 0;
    }
    std::vector<TrayIconRect> rects;
    const std::scoped_lock    lock(_mutex);
    for (const ViewRects& reported : _views) {
        rects.insert(rects.end(), reported.rects.begin(), reported.rects.end());
    }
    const std::optional<RECT> rect = QueriedIconRect(query, _icons, rects, *cursor);
    if (!rect.has_value()) {
        return 0;
    }
    return IconRectAnswer(query.part, *rect);
}

LRESULT ShellNotifyIcon::Forward(HWND sender, const COPYDATASTRUCT& data) {
    HWND explorer = ExplorerTray();
    if (explorer == nullptr) {
        return FALSE;
    }
    COPYDATASTRUCT copy   = data;
    DWORD_PTR      result = 0;
    if (SendMessageTimeoutW(explorer, WM_COPYDATA, reinterpret_cast<WPARAM>(sender), reinterpret_cast<LPARAM>(&copy), SMTO_ABORTIFHUNG, kForwardTimeoutMs, &result) == 0) {
        _forwardLog.Failed("forward_failed", Win32Error("SendMessageTimeoutW(WM_COPYDATA to the taskbar)", HRESULT_FROM_WIN32(GetLastError())));
        return FALSE;
    }
    _forwardLog.Succeeded();
    return static_cast<LRESULT>(result);
}

HWND ShellNotifyIcon::ExplorerTray() {
    if (_explorer != nullptr && IsWindow(_explorer)) {
        return _explorer;
    }
    _explorer = ExplorerTaskbar(_windowClass);
    return _explorer;
}

void ShellNotifyIcon::KeepFirst() {
    if (FindWindowW(_windowClass.c_str(), nullptr) == _window) {
        return;
    }
    if (!SetWindowPos(_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
        ThrowLastError("SetWindowPos(tray)");
    }
}

void ShellNotifyIcon::DropIconsOfClosedWindows() {
    std::vector<TrayIcon> icons = _icons;
    std::erase_if(icons, [](const TrayIcon& icon) { return !IsWindow(ToWindow(icon.owner)); });
    Publish(std::move(icons));
}

void ShellNotifyIcon::Publish(std::vector<TrayIcon> icons) {
    {
        const std::scoped_lock lock(_mutex);
        if (icons == _icons) {
            return;
        }
        _icons = std::move(icons);
    }
    _changes.Notify("tray");
}

void ShellNotifyIcon::Run(std::promise<void>& ready) {
    // WIC reads the icons on this thread.
    std::optional<ComScope> com;
    bool                    started = false;
    try {
        com.emplace(ComThreading::Multi, "tray");
        _imaging = CreateImagingFactory();
        _window  = CreateTrayWindow(_windowClass, WindowProc, this);
        KeepFirst();
        StartTimer(_window, kFirstTimer, kFirstEveryMs, "tray first");
        StartTimer(_window, kSweepTimer, kSweepEveryMs, "tray sweep");
        ready.set_value();
        started = true;
    } catch (...) { ready.set_exception(std::current_exception()); }
    if (started) {
        try {
            RunLoop();
        } catch (...) { Fail(std::current_exception()); }
    }
    // CreateTrayWindow unregisters its class when it throws, leaving the window null.
    if (_window != nullptr) {
        DestroyWindow(_window);
        UnregisterClassW(_windowClass.c_str(), GetModuleHandleW(nullptr));
    }
    _imaging.Reset();
}

// Sent messages, which is how every app's WM_COPYDATA arrives, are delivered inside PeekMessageW.
void ShellNotifyIcon::RunLoop() {
    HANDLE stop = _stopEvent.get();
    for (;;) {
        const DWORD woke = MsgWaitForMultipleObjectsEx(1, &stop, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (woke == WAIT_OBJECT_0) {
            return;
        }
        if (woke != WAIT_OBJECT_0 + 1) {
            ThrowLastError("MsgWaitForMultipleObjectsEx(tray)");
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
    }
}

// Stops the thread and leaves the error for App, which ends the process with it.
void ShellNotifyIcon::Fail(const std::exception_ptr& error) {
    SetEvent(_stopEvent.get());
    _failure.Report(error);
}

void AnnounceTray() {
    if (!SendNotifyMessageW(HWND_BROADCAST, TaskbarCreatedMessage(), 0, 0)) {
        ThrowLastError("SendNotifyMessageW(TaskbarCreated)");
    }
}
