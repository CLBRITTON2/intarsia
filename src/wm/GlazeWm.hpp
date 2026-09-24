#pragma once

#include "platform/Handle.hpp"
#include "platform/WebSocket.hpp"
#include "wm/IWindowManager.hpp"
#include "wm/Native.hpp"

#include <windows.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Backend for GlazeWM over its IPC WebSocket. A receiver thread holds the connection and retries forever while GlazeWM
// is down, reporting Disconnected in the meantime. A sender thread delivers commands, so the UI thread never waits on
// the socket. The focused title comes from Windows events, since GlazeWM sends no event when a title changes.
class GlazeWm final : public IWindowManager {
  public:
    GlazeWm(HWND target, UINT changedMessage);
    ~GlazeWm() override;
    GlazeWm(const GlazeWm&)                                = delete;
    GlazeWm&                     operator=(const GlazeWm&) = delete;

    std::optional<FocusedWindow> Focused() const override;
    std::vector<Workspace>       Workspaces() const override;
    WindowManagerStatus          Status() const override;
    // Queues the command for the sender thread. A name GlazeWM's parser cannot take safely is logged and dropped.
    void FocusWorkspace(const std::string& name) override;

  private:
    void Receive();
    void Session();
    void SendCommands();
    // Every send goes through here: WinHTTP fails a send while another one is pending.
    void                    SendSerialized(WebSocket& socket, std::string_view text);
    void                    Publish(std::vector<Workspace> workspaces, WindowManagerStatus status);

    Native                  native_;
    HWND                    target_;
    UINT                    changedMessage_;
    UniqueHandle            stopEvent_;
    UniqueHandle            commandEvent_;
    mutable std::mutex      mutex_; // guards everything below it up to sendMutex_
    std::vector<Workspace>  workspaces_;
    WindowManagerStatus     status_;
    bool                    stopping_;
    std::deque<std::string> commands_;
    // Shared so a sender or the destructor can still use the socket while the session that opened it is ending.
    std::shared_ptr<WebSocket> socket_;
    std::mutex                 sendMutex_;
    // Declared last so every member above exists before the threads start.
    std::thread receiver_;
    std::thread sender_;
};
