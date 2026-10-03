#pragma once

#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "platform/ThreadFailure.hpp"
#include "tray/ITray.hpp"
#include "tray/NotifyIconProtocol.hpp"

#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// The class of the taskbar window, which Shell_NotifyIcon and SHAppBarMessage send to.
inline constexpr wchar_t kTaskbarClass[] = L"Shell_TrayWnd";

// The notification area, taken over from the taskbar. Shell_NotifyIcon and SHAppBarMessage send WM_COPYDATA to the
// first kTaskbarClass window in z-order, so a hidden topmost window of that class, kept first, receives every app's
// icons. Everything it receives but icon rect queries also goes on to Explorer's taskbar window, which runs the appbars
// and keeps its own icons, so the taskbar has them all back when the bar exits. The window has its own thread, so a slow frame never
// holds up an app's call, and it reads each icon's pixels as the message arrives, while the app's handle is valid.
class ShellNotifyIcon final : public ITray {
  public:
    // windowClass is the class the window registers: kTaskbarClass takes the tray over, and any other makes a tray
    // that only a sender naming that class reaches, as tests do. A failure that stops the thread posts failedMessage
    // to target.
    ShellNotifyIcon(ChangeSignal& changes, std::wstring windowClass, HWND target, std::uint32_t failedMessage);
    ~ShellNotifyIcon() override;
    ShellNotifyIcon(const ShellNotifyIcon&)                 = delete;
    ShellNotifyIcon&      operator=(const ShellNotifyIcon&) = delete;

    std::vector<TrayIcon> Icons() const override;
    // Post the icon's input messages to its app. An app running elevated refuses them (UIPI), which is logged.
    void Press(const TrayIcon& icon, TrayButton button) override;
    void Move(const TrayIcon& icon) override;
    void OpenPopup(const TrayIcon& icon) override;
    void ClosePopup(const TrayIcon& icon) override;
    void ReportIconRects(const void* view, std::vector<TrayIconRect> rects) override;

    // On failedMessage, rethrows the error that stopped the thread.
    void RethrowFailure();

  private:
    // Null while the cursor cannot be read (another desktop has the input), logged once per outage.
    std::optional<POINT> CursorPosition();
    // ERROR_SUCCESS, or the error of the first message the owner refused.
    static DWORD            PostInput(const TrayIcon& icon, const std::vector<TrayInputMessage>& messages);
    void                    PostHover(const TrayIcon& icon, std::uint32_t event, POINT cursor);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleCopyData(HWND sender, const COPYDATASTRUCT& data);
    // Whether the icons took the message, which is what Shell_NotifyIcon returns to the app.
    bool ApplyMessage(const COPYDATASTRUCT& data);
    // The icon's place on a bar, or 0, which shell32 turns into E_FAIL, when no bar shows it. Never Explorer's answer,
    // which is somewhere the icon is not.
    LRESULT AnswerIconRect(const COPYDATASTRUCT& data);
    LRESULT Forward(HWND sender, const COPYDATASTRUCT& data);
    // Explorer's taskbar window, found again once it is gone, or null while Explorer is not running.
    HWND ExplorerTray();
    // Moves the window back to the top when another topmost window rose above it, as windows do when they activate.
    void KeepFirst();
    // Apps that exit or crash without deleting their icons leave them behind.
    void          DropIconsOfClosedWindows();
    void          Publish(std::vector<TrayIcon> icons);
    void          Run(std::promise<void>& ready);
    void          RunLoop();
    void          Fail(const std::exception_ptr& error);

    ChangeSignal& _changes;
    std::wstring  _windowClass;
    ThreadFailure _failure;
    UniqueHandle  _stopEvent;
    // The UI thread's alone, which Press and Move are called on.
    std::optional<TrayClick> _lastClick; // null after a double click, so a third click starts a new pair
    std::optional<POINT>     _lastMove;
    FailureLog               _hoverLog{"tray"};
    FailureLog               _cursorLog{"tray"};
    // The tray thread's alone.
    HWND                                       _window   = nullptr;
    HWND                                       _explorer = nullptr;
    Microsoft::WRL::ComPtr<IWICImagingFactory> _imaging; // released before the thread leaves COM
    FailureLog                                 _messageLog{"tray"};
    FailureLog                                 _iconLog{"tray"};
    FailureLog                                 _ownerLog{"tray"};
    FailureLog                                 _forwardLog{"tray"};
    FailureLog                                 _rectLog{"tray"};
    // Guards _icons and _views. Only the tray's thread writes _icons, so it reads them without the lock.
    mutable std::mutex    _mutex;
    std::vector<TrayIcon> _icons;
    struct ViewRects {
        const void*               view;
        std::vector<TrayIconRect> rects;
    };
    std::vector<ViewRects> _views;
    // Started by the constructor once every member above exists.
    std::thread _thread;
};

// Explorer's window of the class, or null while Explorer shows none. Never this process's own window.
HWND ExplorerTaskbar(const std::wstring& windowClass);

// Tells every app that a taskbar started, as Explorer does after a restart (TaskbarCreated), so each adds its icons
// again and they reach a tray created after them.
void AnnounceTray();
