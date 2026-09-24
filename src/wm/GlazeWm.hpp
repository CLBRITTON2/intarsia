#pragma once

#include "platform/Handle.hpp"
#include "platform/WebSocket.hpp"
#include "wm/IWindowManager.hpp"
#include "wm/Native.hpp"

#include <windows.h>

#include <mutex>
#include <thread>
#include <vector>

// Backend for GlazeWM over its IPC WebSocket. A worker thread holds the connection and retries forever while GlazeWM is
// down, reporting Disconnected in the meantime. The focused title comes from Windows events, since GlazeWM sends no
// event when a window's title changes.
class GlazeWm final : public IWindowManager {
  public:
    GlazeWm(HWND target, UINT changedMessage);
    ~GlazeWm() override;
    GlazeWm(const GlazeWm&)                                = delete;
    GlazeWm&                     operator=(const GlazeWm&) = delete;

    std::optional<FocusedWindow> Focused() const override;
    std::vector<Workspace>       Workspaces() const override;
    WindowManagerStatus          Status() const override;

  private:
    void                   Run();
    void                   Session();
    void                   Publish(std::vector<Workspace> workspaces, WindowManagerStatus status);

    Native                 native_;
    HWND                   target_;
    UINT                   changedMessage_;
    UniqueHandle           stopEvent_;
    mutable std::mutex     mutex_;
    std::vector<Workspace> workspaces_;
    WindowManagerStatus    status_;
    bool                   stopping_;
    // The live connection, so the destructor can abort a Receive the worker is blocked in.
    WebSocket* socket_;
    // Declared last so every member above exists before the thread starts.
    std::thread thread_;
};
