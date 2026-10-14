#pragma once

#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Backend for running without a window manager. Follows the foreground window and its title through WinEvent hooks,
// which Windows delivers on the thread that installed them. WinEvent callbacks carry no context pointer, so only one
// instance may exist at a time: destroy the old one before creating its replacement.
class Native final : public IWindowManager {
  public:
    explicit Native(ChangeSignal& changes);

    std::optional<FocusedWindow> Focused() const override;
    // Windows has no public API that lists virtual desktops, so there are never any. The config loader rejects a
    // workspaces module with this backend.
    std::vector<Workspace> Workspaces() const override;
    // None, for the same reason: they belong to workspaces. The config loader rejects a windows module here too.
    std::vector<ManagedWindow> Windows() const override;
    // None: binding modes and tiling belong to a tiling window manager. The config loader rejects their modules here.
    std::vector<std::wstring>      BindingModes() const override;
    std::optional<TilingDirection> Direction() const override;
    // Always connected: the WinEvents come from the OS itself.
    WindowManagerStatus Status() const override;
    // Throws std::logic_error: there are no workspaces to focus.
    void FocusWorkspace(const std::string& name) override;
    // Throws std::logic_error: there are no windows to focus.
    void FocusWindow(const std::string& id) override;
    // Throws std::logic_error: nothing tiles.
    void ToggleTilingDirection() override;
    // Throws std::logic_error: Windows takes no commands. The config loader rejects window_manager actions here.
    void RunCommand(const std::string& command) override;

  private:
    // Registers the instance the callbacks reach for as long as it lives, and refuses a second one.
    class ActiveInstance {
      public:
        explicit ActiveInstance(Native* instance);
        ~ActiveInstance();
        ActiveInstance(const ActiveInstance&)            = delete;
        ActiveInstance& operator=(const ActiveInstance&) = delete;
    };

    using UniqueHook = UniqueReleased<HWINEVENTHOOK, &UnhookWinEvent>;

    static UniqueHook    HookEvent(std::uint32_t event, std::uint32_t process, std::uint32_t thread);
    static UniqueHook    HookTitleChanges(HWND window);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread, DWORD time);
    void                 Refocus(HWND window);
    void                 Retitle();

    ActiveInstance       _active;
    ChangeSignal&        _changes;
    HWND                 _foreground;
    std::wstring         _title;
    UniqueHook           _foregroundHook;
    UniqueHook           _titleHook;
};

// The text of a window's title bar, empty when it has none, made safe to draw (DrawableText), since any process can put
// anything in its own title.
std::wstring WindowTitle(HWND window);
