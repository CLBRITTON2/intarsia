#pragma once

#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>

// Backend for running without a window manager. Follows the foreground window and its title through WinEvent hooks,
// which Windows delivers on the thread that installed them. WinEvent callbacks carry no context pointer, so only one
// instance may exist at a time: destroy the old one before creating its replacement.
class NativeWindowManager final : public IWindowManager {
  public:
    NativeWindowManager(HWND target, UINT changedMessage);

    std::optional<FocusedWindow> Focused() const override;

  private:
    // Registers the instance the callbacks reach for as long as it lives, and refuses a second one.
    class ActiveInstance {
      public:
        explicit ActiveInstance(NativeWindowManager* instance);
        ~ActiveInstance();
        ActiveInstance(const ActiveInstance&)            = delete;
        ActiveInstance& operator=(const ActiveInstance&) = delete;
    };

    struct Unhook {
        void operator()(HWINEVENTHOOK hook) const {
            UnhookWinEvent(hook);
        }
    };
    using UniqueHook = std::unique_ptr<std::remove_pointer_t<HWINEVENTHOOK>, Unhook>;

    static UniqueHook    HookEvent(DWORD event);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread, DWORD time);
    void                 Refocus(HWND window);

    ActiveInstance       active_;
    HWND                 target_;
    UINT                 changedMessage_;
    HWND                 foreground_;
    UniqueHook           foregroundHook_;
    UniqueHook           nameHook_;
};

// The text of a window's title bar, empty when it has none.
std::wstring WindowTitle(HWND window);
