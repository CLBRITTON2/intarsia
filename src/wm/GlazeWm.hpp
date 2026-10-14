#pragma once

#include "platform/Handle.hpp"
#include "platform/WebSocket.hpp"
#include "platform/WorkQueue.hpp"
#include "wm/IWindowManager.hpp"
#include "wm/Native.hpp"

#include <windows.h>

#include <cstdint>
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
    // port is GlazeWM's IPC port on 127.0.0.1, 6123 unless a test server stands in. focus follows the focused window,
    // or is null when nothing shows it, since its hooks wake the bar for every focus change and retitle.
    GlazeWm(ChangeSignal& changes, std::uint16_t port, std::unique_ptr<Native> focus);
    ~GlazeWm() override;
    GlazeWm(const GlazeWm&)                                  = delete;
    GlazeWm&                       operator=(const GlazeWm&) = delete;

    std::optional<FocusedWindow>   Focused() const override;
    std::vector<Workspace>         Workspaces() const override;
    std::vector<ManagedWindow>     Windows() const override;
    std::vector<std::wstring>      BindingModes() const override;
    std::optional<TilingDirection> Direction() const override;
    WindowManagerStatus            Status() const override;
    // A name GlazeWM's parser cannot take safely is logged and dropped.
    void FocusWorkspace(const std::string& name) override;
    // An id that is not a UUID is logged and dropped.
    void FocusWindow(const std::string& id) override;
    void ToggleTilingDirection() override;
    // Sends "command <command>". A command holding a control character is logged and dropped.
    void RunCommand(const std::string& command) override;

  private:
    // Everything the backend reports, replaced whole so readers never see half of one update.
    struct Snapshot {
        std::vector<Workspace>         workspaces;
        std::vector<ManagedWindow>     windows;
        std::vector<std::wstring>      bindingModes;
        std::optional<TilingDirection> direction;
        WindowManagerStatus            status;

        // No workspaces, windows, binding modes or direction yet.
        static Snapshot Empty(Connection connection, std::string detail);
        bool            operator==(const Snapshot&) const = default;
    };

    void Receive();
    void Session();
    void SendCommand(const std::string& command);
    // Every send goes through here: WinHTTP fails a send while another one is pending.
    void SendSerialized(WebSocket& socket, std::string_view text);
    void Publish(const Snapshot& snapshot);
    // Queues a command built by makeCommand, which throws ProtocolError for an argument GlazeWM cannot take.
    void                    QueueCommand(const std::function<std::string()>& makeCommand);

    std::unique_ptr<Native> _focus;
    std::uint16_t           _port;
    ChangeSignal&           _changes;
    UniqueHandle            _stopEvent;
    mutable std::mutex      _mutex; // guards the members below it up to _sendMutex
    Snapshot                _snapshot;
    bool                    _stopping = false;
    // Shared so the sender or the destructor can still use the socket while the session that opened it is ending.
    std::shared_ptr<WebSocket> _socket;
    std::mutex                 _sendMutex;
    // Held by pointer so the destructor can stop it, with no send pending, before it closes the socket.
    std::unique_ptr<WorkQueue<std::string>> _sender;
    // Declared last so every member above exists before the thread starts.
    std::thread _receiver;
};
