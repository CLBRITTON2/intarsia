#include "tray/ShellNotifyIcon.hpp"

#include "platform/Errors.hpp"
#include "platform/FileWatcher.hpp"
#include "platform/PeerProcess.hpp"
#include "platform/Win32Cast.hpp"
#include "render/Icon.hpp"
#include "tray/NotifyIconProtocol.hpp"

#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
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
        HINSTANCE   instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW registration{};
        registration.cbSize        = sizeof(registration);
        registration.lpfnWndProc   = procedure;
        registration.hInstance     = instance;
        registration.lpszClassName = windowClass.c_str();
        if (RegisterClassExW(&registration) == 0) {
            ThrowLastError("RegisterClassExW(tray)");
        }
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, windowClass.c_str(), L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, owner);
        if (window == nullptr) {
            const DWORD error = GetLastError();
            UnregisterClassW(windowClass.c_str(), instance);
            throw Win32Error("CreateWindowExW(tray)", HRESULT_FROM_WIN32(error));
        }
        return window;
    }

    // Frees a SHAllocShared handle that process holds.
    struct FreeShared {
        DWORD process;

        void  operator()(HANDLE handle) const {
            SHFreeShared(handle, process);
        }
    };

    bool IsOwnWindow(HWND window) {
        DWORD processId = 0;
        return GetWindowThreadProcessId(window, &processId) != 0 && processId == GetCurrentProcessId();
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

} // namespace

ShellNotifyIcon::ShellNotifyIcon(ChangeSignal& changes, std::wstring windowClass, HWND target, std::uint32_t failedMessage) :
    _changes(changes), _windowClass(std::move(windowClass)), _target(target), _failedMessage(failedMessage),
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
    const POINT     cursor = CursorPosition();
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
    const POINT cursor = CursorPosition();
    if (_lastMove.has_value() && _lastMove->x == cursor.x && _lastMove->y == cursor.y) {
        return;
    }
    _lastMove = cursor;
    PostHover(icon, WM_MOUSEMOVE, cursor);
}

void ShellNotifyIcon::OpenPopup(const TrayIcon& icon) {
    PostHover(icon, NIN_POPUPOPEN, CursorPosition());
}

void ShellNotifyIcon::ClosePopup(const TrayIcon& icon) {
    PostHover(icon, NIN_POPUPCLOSE, CursorPosition());
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

POINT ShellNotifyIcon::CursorPosition() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        ThrowLastError("GetCursorPos");
    }
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

std::exception_ptr ShellNotifyIcon::TakeError() {
    const std::scoped_lock lock(_mutex);
    return std::exchange(_error, nullptr);
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

// Shell_NotifyIcon gets the tray's answer, and every other message (appbars, icon positions) Explorer's.
LRESULT ShellNotifyIcon::HandleCopyData(HWND sender, const COPYDATASTRUCT& data) {
    if (data.dwData == kAppBarCopyData && IsOwnWindow(sender)) {
        return ForwardOwnAppBar(sender, data);
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
    std::shared_ptr<const IconPixels> pixels;
    if ((message.flags & NIF_ICON) != 0 && message.icon != 0) {
        try {
            pixels = std::make_shared<const IconPixels>(IconToPixels(IntToPointer<std::remove_pointer_t<HICON>>(message.icon)));
            _iconLog.Succeeded();
        } catch (const Win32Error& error) {
            // The icon stays without pixels, and so hidden, until the app sets one that reads.
            _iconLog.Failed("icon_unreadable", error);
        }
    }
    std::optional<std::vector<TrayIcon>> icons = ApplyNotifyIcon(Icons(), message, std::move(pixels));
    if (!icons.has_value()) {
        return false;
    }
    Publish(std::move(*icons));
    return true;
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

// shell32 sees that the first taskbar window is this process's own, so the bars' SHAppBarMessage calls put a pointer to
// their result in place of a SHAllocShared handle. Explorer cannot open it and drops ABM_QUERYPOS and ABM_SETPOS
// without an error, which leaves every work area full size. The result goes through a block Explorer can open, and is
// copied back through the pointer, which stays valid while the bar's thread waits on this message.
LRESULT ShellNotifyIcon::ForwardOwnAppBar(HWND sender, const COPYDATASTRUCT& data) {
    const std::span<const std::byte> payload(static_cast<const std::byte*>(data.lpData), data.cbData);
    AppBarCommand                    command{};
    try {
        command = ParseAppBarCommand(payload);
        _messageLog.Succeeded();
    } catch (const TrayMessageError& error) {
        _messageLog.Failed("malformed_appbar_message", error);
        return FALSE;
    }
    if (command.result == 0) {
        return Forward(sender, data);
    }
    void*        target = IntToPointer<void>(static_cast<LONG_PTR>(command.result));
    AppBarData32 result{};
    SIZE_T       copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), target, &result, sizeof(result), &copied) || copied != sizeof(result)) {
        _appBarLog.Failed("appbar_result_unreadable", Win32Error("ReadProcessMemory(appbar result)", HRESULT_FROM_WIN32(GetLastError())));
        return FALSE;
    }
    // shell32 sets only the size of the result it waits on, so the request fills the rest.
    if (result.size != sizeof(AppBarData32)) {
        _appBarLog.Failed("appbar_result_unexpected",
                          std::runtime_error(std::format("the appbar result at 0x{:x} holds size {}, expected {}", command.result, result.size, sizeof(AppBarData32))));
        return FALSE;
    }
    DWORD explorerProcess = 0;
    HWND  explorer        = ExplorerTray();
    if (explorer == nullptr || GetWindowThreadProcessId(explorer, &explorerProcess) == 0) {
        return FALSE;
    }
    // Made for Explorer and named as Explorer's: SHAllocShared for this process's own id also returns a value that only
    // this process can open.
    result = command.data;
    const std::unique_ptr<void, FreeShared> inExplorer(SHAllocShared(&result, sizeof(result), explorerProcess), FreeShared{.process = explorerProcess});
    if (inExplorer == nullptr) {
        ThrowLastError("SHAllocShared(appbar result)");
    }
    std::vector<std::byte> translated = WithAppBarResult(payload, reinterpret_cast<std::uint64_t>(inExplorer.get()), explorerProcess);
    COPYDATASTRUCT         copy       = data;
    copy.lpData                       = translated.data();
    const LRESULT answer              = Forward(sender, copy);
    void*         view                = SHLockShared(inExplorer.get(), explorerProcess);
    if (view == nullptr) {
        ThrowLastError("SHLockShared(appbar result)");
    }
    std::memcpy(&result, view, sizeof(result));
    SHUnlockShared(view);
    if (!WriteProcessMemory(GetCurrentProcess(), target, &result, sizeof(result), &copied) || copied != sizeof(result)) {
        _appBarLog.Failed("appbar_result_unwritable", Win32Error("WriteProcessMemory(appbar result)", HRESULT_FROM_WIN32(GetLastError())));
        return FALSE;
    }
    _appBarLog.Succeeded();
    return answer;
}

HWND ShellNotifyIcon::ExplorerTray() {
    if (_explorer != nullptr && IsWindow(_explorer)) {
        return _explorer;
    }
    _explorer = nullptr;
    for (HWND candidate = FindWindowExW(nullptr, nullptr, _windowClass.c_str(), nullptr); candidate != nullptr;
         candidate      = FindWindowExW(nullptr, candidate, _windowClass.c_str(), nullptr)) {
        if (candidate != _window && IsExplorer(candidate)) {
            _explorer = candidate;
            break;
        }
    }
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
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        ThrowIfFailed(initialized, "CoInitializeEx(tray)");
        _window = CreateTrayWindow(_windowClass, WindowProc, this);
    } catch (...) {
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
        ready.set_exception(std::current_exception());
        return;
    }
    try {
        KeepFirst();
        if (SetTimer(_window, kFirstTimer, kFirstEveryMs, nullptr) == 0) {
            ThrowLastError("SetTimer(tray first)");
        }
        if (SetTimer(_window, kSweepTimer, kSweepEveryMs, nullptr) == 0) {
            ThrowLastError("SetTimer(tray sweep)");
        }
        ready.set_value();
    } catch (...) {
        DestroyWindow(_window);
        UnregisterClassW(_windowClass.c_str(), GetModuleHandleW(nullptr));
        CoUninitialize();
        ready.set_exception(std::current_exception());
        return;
    }
    try {
        RunLoop();
    } catch (...) { Fail(std::current_exception()); }
    DestroyWindow(_window);
    UnregisterClassW(_windowClass.c_str(), GetModuleHandleW(nullptr));
    CoUninitialize();
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
void ShellNotifyIcon::Fail(std::exception_ptr error) {
    {
        const std::scoped_lock lock(_mutex);
        if (_error) {
            return;
        }
        _error = error;
    }
    SetEvent(_stopEvent.get());
    if (!PostMessageW(_target, _failedMessage, 0, 0)) {
        Log(LogLevel::Error, "tray", "failure_unreported", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}

void AnnounceTray() {
    const UINT created = RegisterWindowMessageW(L"TaskbarCreated");
    if (created == 0) {
        ThrowLastError("RegisterWindowMessageW(TaskbarCreated)");
    }
    if (!SendNotifyMessageW(HWND_BROADCAST, created, 0, 0)) {
        ThrowLastError("SendNotifyMessageW(TaskbarCreated)");
    }
}
