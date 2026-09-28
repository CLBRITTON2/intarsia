#pragma once

#include "platform/ChangeSignal.hpp"
#include "platform/Handle.hpp"
#include "platform/Log.hpp"
#include "tray/ITray.hpp"

#include <windows.h>

#include <cstdint>
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The class of the taskbar window, which Shell_NotifyIcon and SHAppBarMessage send to.
inline constexpr wchar_t kTaskbarClass[] = L"Shell_TrayWnd";

// The notification area, taken over from the taskbar. Shell_NotifyIcon and SHAppBarMessage send WM_COPYDATA to the
// first kTaskbarClass window in z-order, so a hidden topmost window of that class, kept first, receives every app's
// icons. Everything it receives also goes on to Explorer's taskbar window, which runs the appbars and keeps its own
// icons, so the taskbar has them all back when the bar exits. The window has its own thread, so a slow frame never
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
    // Posts the icon's input messages to its app. An app running elevated refuses them (UIPI), which is logged.
    void Press(const TrayIcon& icon, TrayButton button) override;

    // The error that stopped the thread, set before failedMessage is posted.
    std::exception_ptr TakeError();

  private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT                 HandleCopyData(HWND sender, const COPYDATASTRUCT& data);
    // Whether the icons took the message, which is what Shell_NotifyIcon returns to the app.
    bool    ApplyMessage(const COPYDATASTRUCT& data);
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
    void          Fail(std::exception_ptr error);

    ChangeSignal& _changes;
    std::wstring  _windowClass;
    HWND          _target;
    std::uint32_t _failedMessage;
    UniqueHandle  _stopEvent;
    // The tray thread's alone.
    HWND       _window   = nullptr;
    HWND       _explorer = nullptr;
    FailureLog _messageLog{"tray"};
    FailureLog _iconLog{"tray"};
    FailureLog _forwardLog{"tray"};
    // Guards _icons and _error.
    mutable std::mutex    _mutex;
    std::vector<TrayIcon> _icons;
    std::exception_ptr    _error;
    // Started by the constructor once every member above exists.
    std::thread _thread;
};

// Tells every app that a taskbar started, as Explorer does after a restart (TaskbarCreated), so each adds its icons
// again and they reach a tray created after them.
void AnnounceTray();
