#include "wm/Native.hpp"

#include "platform/Errors.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

    Native* activeInstance = nullptr;

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

Native::Native(HWND target, UINT changedMessage) :
    active_(this), target_(target), changedMessage_(changedMessage), foreground_(GetForegroundWindow()), foregroundHook_(HookEvent(EVENT_SYSTEM_FOREGROUND)),
    nameHook_(HookEvent(EVENT_OBJECT_NAMECHANGE)) {}

std::optional<FocusedWindow> Native::Focused() const {
    if (foreground_ == nullptr) {
        return std::nullopt;
    }
    return FocusedWindow{.title = WindowTitle(foreground_)};
}

std::vector<Workspace> Native::Workspaces() const {
    return {};
}

WindowManagerStatus Native::Status() const {
    return WindowManagerStatus{.name = L"Windows", .connection = Connection::Connected, .detail = ""};
}

Native::UniqueHook Native::HookEvent(DWORD event) {
    HWINEVENTHOOK hook = SetWinEventHook(event, event, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook == nullptr) {
        ThrowLastError("SetWinEventHook");
    }
    return UniqueHook(hook);
}

void CALLBACK Native::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
    if (activeInstance == nullptr || object != OBJID_WINDOW) {
        return;
    }
    // Title changes matter only for the window the bar is showing.
    if (event == EVENT_SYSTEM_FOREGROUND || window == activeInstance->foreground_) {
        activeInstance->Refocus(window);
    }
}

// Only records the change and posts: this runs inside a WinEvent callback, where an exception could not be caught.
void Native::Refocus(HWND window) {
    foreground_ = window;
    if (!PostMessageW(target_, changedMessage_, 0, 0)) {
        std::fprintf(stderr, "intarsia: warning component=native event=post_failed error=%lu\n", GetLastError());
    }
}

std::wstring WindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int    copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<size_t>(copied > 0 ? copied : 0));
    return title;
}
