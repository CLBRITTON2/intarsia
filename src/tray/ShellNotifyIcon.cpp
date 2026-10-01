#include "tray/ShellNotifyIcon.hpp"

#include "platform/AppBar.hpp"
#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/Handle.hpp"
#include "platform/PeerProcess.hpp"
#include "platform/Win32Cast.hpp"
#include "platform/Win32Window.hpp"
#include "render/Icon.hpp"
#include "tray/NotifyIconProtocol.hpp"

#include <shellapi.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    // Often enough that an app's message rarely goes to Explorer alone after another topmost window rose above ours.
    constexpr UINT_PTR      kFirstTimer   = 1;
    constexpr std::uint32_t kFirstEveryMs = 100;
    constexpr UINT_PTR      kSweepTimer   = 2;
    constexpr std::uint32_t kSweepEveryMs = 5000;
    // The sender waits on the forward, so a hung Explorer must not hold it for long.
    constexpr std::uint32_t kForwardTimeoutMs = 2000;

    HWND                    ToWindow(std::intptr_t handle) {
        return IntToPointer<std::remove_pointer_t<HWND>>(handle);
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

    // Another intarsia, or any other tray that forwards, would send the message straight back.
    bool IsExplorer(HWND window) {
        DWORD processId = 0;
        if (GetWindowThreadProcessId(window, &processId) == 0 || processId == GetCurrentProcessId()) {
            return false;
        }
        // Null for a process of another user, whose taskbar is not this desktop's anyway.
        HANDLE raw = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (raw == nullptr) {
            return false;
        }
        const UniqueHandle process(raw);
        try {
            return SameFileName(ProcessImage(process.get()).filename().wstring(), L"explorer.exe");
        } catch (const Win32Error& error) {
            Log(LogLevel::Warning, "tray", "taskbar_unreadable", {{.key = "process", .value = std::to_string(processId)}, {.key = "reason", .value = error.what()}});
            return false;
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

ShellNotifyIcon::ShellNotifyIcon(ChangeSignal& changes, std::wstring windowClass, HWND target, std::uint32_t failedMessage) :
    _changes(changes), _windowClass(std::move(windowClass)), _failure(target, failedMessage, "tray"),
    _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(tray stop)")) {
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
    const std::optional<POINT> position = CursorPosition();
    if (!position.has_value()) {
        return;
    }
    const POINT     cursor = *position;
    const TrayClick click{.owner = icon.owner, .id = icon.id, .button = button, .cursor = cursor, .time = std::chrono::steady_clock::now()};
    const bool      doubled =
        IsDoubleClick(_lastClick, click, std::chrono::milliseconds(GetDoubleClickTime()), SIZE{.cx = GetSystemMetrics(SM_CXDOUBLECLK), .cy = GetSystemMetrics(SM_CYDOUBLECLK)});
    // A third click after a double click starts a new pair, as Windows counts them.
    _lastClick                                   = doubled ? std::nullopt : std::optional<TrayClick>(click);
    const std::vector<TrayInputMessage> messages = TrayInputMessages(icon, doubled ? DoubleClickEvents(button) : ClickEvents(button, icon.version), cursor);
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
    const std::optional<POINT> cursor = CursorPosition();
    if (!cursor.has_value() || (_lastMove.has_value() && _lastMove->x == cursor->x && _lastMove->y == cursor->y)) {
        return;
    }
    _lastMove = cursor;
    PostHover(icon, WM_MOUSEMOVE, *cursor);
}

void ShellNotifyIcon::OpenPopup(const TrayIcon& icon) {
    const std::optional<POINT> cursor = CursorPosition();
    if (cursor.has_value()) {
        PostHover(icon, NIN_POPUPOPEN, *cursor);
    }
}

void ShellNotifyIcon::ClosePopup(const TrayIcon& icon) {
    const std::optional<POINT> cursor = CursorPosition();
    if (cursor.has_value()) {
        PostHover(icon, NIN_POPUPCLOSE, *cursor);
    }
}

void ShellNotifyIcon::ShowRects(const void* view, std::vector<TrayIconRect> rects) {
    const std::scoped_lock lock(_mutex);
    std::erase_if(_views, [view](const View& shown) { return shown.view == view; });
    if (!rects.empty()) {
        _views.push_back(View{.view = view, .rects = std::move(rects)});
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

// Null while the cursor cannot be read, which is so on another desktop (the lock screen, a UAC prompt): the bar still
// redraws and hovers there, and an app told of input it cannot see would be wrong anyway.
std::optional<POINT> ShellNotifyIcon::CursorPosition() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        _cursorLog.Failed("cursor_unavailable", Win32Error("GetCursorPos", HRESULT_FROM_WIN32(GetLastError())));
        return std::nullopt;
    }
    _cursorLog.Succeeded();
    return cursor;
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
    return applied ? TRUE : FALSE;
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
            pixels = std::make_shared<const IconPixels>(IconToPixels(*_imaging.Get(), IntToPointer<std::remove_pointer_t<HICON>>(message.icon)));
            _iconLog.Succeeded();
        } catch (const Win32Error& error) {
            // The icon stays without pixels, and so hidden, until the app sets one that reads.
            _iconLog.Failed("icon_unreadable", error);
        }
    }
    std::optional<std::vector<TrayIcon>> icons = ApplyNotifyIcon(Icons(), message, ownerImage, std::move(pixels));
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
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        _rectLog.Failed("cursor_unavailable", Win32Error("GetCursorPos(tray icon rect)", HRESULT_FROM_WIN32(GetLastError())));
        return 0;
    }
    _rectLog.Succeeded();
    std::vector<TrayIconRect> rects;
    const std::scoped_lock    lock(_mutex);
    for (const View& view : _views) {
        rects.insert(rects.end(), view.rects.begin(), view.rects.end());
    }
    const std::optional<RECT> rect = QueriedIconRect(query, _icons, rects, cursor);
    return rect.has_value() ? IconRectAnswer(query.part, *rect) : 0;
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
    std::vector<TrayIcon> icons = Icons();
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
    try {
        com.emplace(ComThreading::Multi, "tray");
        _imaging = CreateImagingFactory();
        _window  = CreateTrayWindow(_windowClass, WindowProc, this);
    } catch (...) {
        _imaging.Reset();
        ready.set_exception(std::current_exception());
        return;
    }
    try {
        KeepFirst();
        StartTimer(_window, kFirstTimer, kFirstEveryMs, "tray first");
        StartTimer(_window, kSweepTimer, kSweepEveryMs, "tray sweep");
        ready.set_value();
    } catch (...) {
        DestroyWindow(_window);
        UnregisterClassW(_windowClass.c_str(), GetModuleHandleW(nullptr));
        _imaging.Reset();
        ready.set_exception(std::current_exception());
        return;
    }
    try {
        RunLoop();
    } catch (...) { Fail(std::current_exception()); }
    DestroyWindow(_window);
    UnregisterClassW(_windowClass.c_str(), GetModuleHandleW(nullptr));
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

HWND ExplorerTaskbar(const std::wstring& windowClass) {
    for (HWND candidate = FindWindowExW(nullptr, nullptr, windowClass.c_str(), nullptr); candidate != nullptr;
         candidate      = FindWindowExW(nullptr, candidate, windowClass.c_str(), nullptr)) {
        if (IsExplorer(candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

void AnnounceTray() {
    if (!SendNotifyMessageW(HWND_BROADCAST, TaskbarCreatedMessage(), 0, 0)) {
        ThrowLastError("SendNotifyMessageW(TaskbarCreated)");
    }
}
