#include "wm/NativeWindowManager.hpp"

#include "platform/Errors.hpp"

#include <stdexcept>
#include <string>

namespace {

    NativeWindowManager* activeInstance = nullptr;

} // namespace

NativeWindowManager::ActiveInstance::ActiveInstance(NativeWindowManager* instance) {
    if (activeInstance != nullptr) {
        throw std::logic_error("a NativeWindowManager already exists, destroy it before creating another");
    }
    activeInstance = instance;
}

NativeWindowManager::ActiveInstance::~ActiveInstance() {
    activeInstance = nullptr;
}

NativeWindowManager::NativeWindowManager(HWND target, UINT changedMessage) :
    active_(this), target_(target), changedMessage_(changedMessage), foreground_(GetForegroundWindow()), foregroundHook_(HookEvent(EVENT_SYSTEM_FOREGROUND)),
    nameHook_(HookEvent(EVENT_OBJECT_NAMECHANGE)) {}

std::optional<FocusedWindow> NativeWindowManager::Focused() const {
    if (foreground_ == nullptr) {
        return std::nullopt;
    }
    return FocusedWindow{.title = WindowTitle(foreground_)};
}

NativeWindowManager::UniqueHook NativeWindowManager::HookEvent(DWORD event) {
    HWINEVENTHOOK hook = SetWinEventHook(event, event, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook == nullptr) {
        ThrowLastError("SetWinEventHook");
    }
    return UniqueHook(hook);
}

void CALLBACK NativeWindowManager::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
    if (activeInstance == nullptr || object != OBJID_WINDOW) {
        return;
    }
    // Title changes matter only for the window the bar is showing.
    if (event == EVENT_SYSTEM_FOREGROUND || window == activeInstance->foreground_) {
        activeInstance->Refocus(window);
    }
}

// Only records the change and posts: this runs inside a WinEvent callback, where an exception could not be caught.
void NativeWindowManager::Refocus(HWND window) {
    foreground_ = window;
    PostMessageW(target_, changedMessage_, 0, 0);
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
