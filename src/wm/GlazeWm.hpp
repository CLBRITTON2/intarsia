#pragma once

#include "platform/Handle.hpp"
#include "platform/WebSocket.hpp"
#include "platform/WorkQueue.hpp"
#include "wm/IWindowManager.hpp"
#include "wm/Native.hpp"

#include <windows.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Backend for GlazeWM over its IPC WebSocket. A receiver thread holds the connection and retries forever while GlazeWM
// is down, reporting Disconnected in the meantime. A sender queue delivers commands, so the UI thread never waits on
// the socket. The focused title comes from Windows events, since GlazeWM sends no event when a title changes.
class GlazeWm final : public IWindowManager {
  public:
    GlazeWm(HWND target, UINT changedMessage);
    ~GlazeWm() override;
    GlazeWm(const GlazeWm&)                                = delete;
    GlazeWm&                     operator=(const GlazeWm&) = delete;

    std::optional<FocusedWindow> Focused() const override;
    std::vector<Workspace>       Workspaces() const override;
    std::vector<ManagedWindow>   Windows() const override;
    WindowManagerStatus          Status() const override;
    // Queues the command for the sender. A name GlazeWM's parser cannot take safely is logged and dropped.
    void FocusWorkspace(const std::string& name) override;
    // Queues the command for the sender. An id that is not a UUID is logged and dropped.
    void FocusWindow(const std::string& id) override;

  private:
    void Receive();
    void Session();
    void SendCommand(const std::string& command);
    // Every send goes through here: WinHTTP fails a send while another one is pending.
    void SendSerialized(WebSocket& socket, std::string_view text);
    void Publish(std::vector<Workspace> workspaces, std::vector<ManagedWindow> windows, WindowManagerStatus status);
    // Queues a command built by makeCommand, which throws ProtocolError for an argument GlazeWM cannot take.
    void                       QueueCommand(const std::function<std::string()>& makeCommand);

    Native                     _native;
    HWND                       _target;
    UINT                       _changedMessage;
    UniqueHandle               _stopEvent;
    mutable std::mutex         _mutex; // guards the members below it up to _sendMutex
    std::vector<Workspace>     _workspaces;
    std::vector<ManagedWindow> _windows;
    WindowManagerStatus        _status;
    bool                       _stopping = false;
    // Shared so the sender or the destructor can still use the socket while the session that opened it is ending.
    std::shared_ptr<WebSocket> _socket;
    std::mutex                 _sendMutex;
    // Held by pointer so the destructor can stop it, with no send pending, before it closes the socket.
    std::unique_ptr<WorkQueue<std::string>> _sender;
    // Declared last so every member above exists before the thread starts.
    std::thread _receiver;
};
