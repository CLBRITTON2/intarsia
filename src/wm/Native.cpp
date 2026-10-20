#include "wm/Native.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

    Native* activeInstance = nullptr;

    bool IsConsoleWindow(HWND window) {
        std::array<wchar_t, 32> name{};
        const std::int32_t length = GetClassNameW(window, name.data(), static_cast<std::int32_t>(name.size()));
        return std::wstring_view(name.data(), static_cast<size_t>(std::max(length, 0))) == L"ConsoleWindowClass";
    }

} // namespace

Native::ActiveInstance::ActiveInstance(Native* instance) {
    if (activeInstance != nullptr) {
        throw std::logic_error("a Native backend already exists, destroy it before creating another");
    }
    activeInstance = instance;
}

Native::ActiveInstance::~ActiveInstance() {
    activeInstance = nullptr;
}

Native::Native(ChangeSignal& changes) :
    _active(this), _changes(changes), _foreground(GetForegroundWindow()), _title(WindowTitle(_foreground)), _foregroundHook(HookEvent(EVENT_SYSTEM_FOREGROUND, 0, 0)),
    _titleHook(HookTitleChanges(_foreground)) {}

std::optional<FocusedWindow> Native::Focused() const {
    if (_foreground == nullptr) {
        return std::nullopt;
    }
    return FocusedWindow{.title = _title};
}

std::vector<Workspace> Native::Workspaces() const {
    return {};
}

std::vector<ManagedWindow> Native::Windows() const {
    return {};
}

std::vector<std::wstring> Native::BindingModes() const {
    return {};
}

std::optional<TilingDirection> Native::Direction() const {
    return std::nullopt;
}

WindowManagerStatus Native::Status() const {
    return WindowManagerStatus{
        .name = L"Windows",
        .connection = Connection::Connected,
        .detail = "",
    };
}

void Native::FocusWorkspace(const std::string& name) {
    throw std::logic_error("the native backend has no workspaces, cannot focus '" + name + "'");
}

void Native::FocusWindow(const std::string& id) {
    throw std::logic_error("the native backend lists no windows, cannot focus '" + id + "'");
}

void Native::ToggleTilingDirection() {
    throw std::logic_error("the native backend does not tile, there is no tiling direction to toggle");
}

void Native::RunCommand(const std::string& command) {
    throw std::logic_error("the native backend takes no commands, cannot run '" + command + "'");
}

// A process or thread of 0 hooks every one.
Native::UniqueHook Native::HookEvent(std::uint32_t event, std::uint32_t process, std::uint32_t thread) {
    HWINEVENTHOOK hook = SetWinEventHook(event, event, nullptr, OnWinEvent, process, thread, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook == nullptr) {
        ThrowLastError("SetWinEventHook");
    }
    return UniqueHook(hook);
}

// Hooks only the window's own thread: a wider hook wakes the bar for each name change of every other window and
// control there. A console window is the exception: Windows reports its client's process and thread while conhost
// raises its events, so a hook on those fails with ERROR_INVALID_THREAD_ID and only a hook on every thread sees it.
Native::UniqueHook Native::HookTitleChanges(HWND window) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (thread == 0 || process == 0) {
        return UniqueHook(nullptr);
    }
    if (IsConsoleWindow(window)) {
        return HookEvent(EVENT_OBJECT_NAMECHANGE, 0, 0);
    }
    return HookEvent(EVENT_OBJECT_NAMECHANGE, process, thread);
}

void CALLBACK Native::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (activeInstance == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF) {
        return;
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        activeInstance->Refocus(window);
    } else if (window == activeInstance->_foreground) {
        activeInstance->Retitle();
    }
}

// Runs inside a WinEvent callback, where an exception could not be caught, so a failed hook is logged. The title then
// stays as it was until the next foreground change hooks again.
void Native::Refocus(HWND window) {
    _foreground = window;
    _title = WindowTitle(window);
    try {
        _titleHook = HookTitleChanges(window);
    } catch (const Win32Error& error) {
        _titleHook.reset();
        Log(LogLevel::Error, "native", "title_hook_failed", {{.key = "reason", .value = error.what()}});
    }
    _changes.Notify("native");
}

// Windows also reports name changes that leave the title as it was, and those need no redraw.
void Native::Retitle() {
    std::wstring title = WindowTitle(_foreground);
    if (title == _title) {
        return;
    }
    _title = std::move(title);
    _changes.Notify("native");
}

std::wstring WindowTitle(HWND window) {
    const std::int32_t length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const std::int32_t copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<size_t>(std::max(copied, 0)));
    return DrawableText(std::wstring_view(title));
}
