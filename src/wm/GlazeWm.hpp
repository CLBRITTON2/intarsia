#pragma once

#include "platform/Handle.hpp"
#include "platform/Threads.hpp"
#include "platform/WebSocket.hpp"
#include "wm/IWindowManager.hpp"
#include "wm/Native.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// GlazeWM's IPC: text commands such as "query monitors" over a WebSocket on 127.0.0.1:6123, answered with JSON.
// Shapes follow glzr-io/glazewm packages/wm-common (ipc.rs, wm_event.rs, dtos/), checked against v3.10.1.
// Everything the server sends is untrusted: errors quote at most a short escaped excerpt of it.
namespace glazewm {

    inline constexpr std::string_view kQueryMonitors = "query monitors";
    inline constexpr std::string_view kQueryBindingModes = "query binding-modes";
    inline constexpr std::string_view kQueryTilingDirection = "query tiling-direction";
    inline constexpr std::string_view kToggleTilingDirection = "command toggle-tiling-direction";

    inline constexpr std::string_view kBindingModesChanged = "binding_modes_changed";
    inline constexpr std::string_view kTilingDirectionChanged = "tiling_direction_changed";

    // The events that can change the workspace list, the windows on displayed workspaces, or focus. Subscribing to
    // these alone skips noisy ones like user_config_changed, which carries GlazeWM's whole config.
    inline constexpr std::array<std::string_view, 10> kWorkspaceEvents{"focus_changed",    "workspace_activated",    "workspace_deactivated", "workspace_updated",
                                                                       "monitor_added",    "monitor_removed",        "monitor_updated",       "window_managed",
                                                                       "window_unmanaged", "focused_container_moved"};

    class ProtocolError : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    enum class MessageKind : std::uint8_t {
        Response, // "client_response", the answer to a message the client sent
        Event     // "event_subscription", pushed for a subscription
    };

    struct ServerMessage {
        MessageKind kind;
        std::string clientMessage; // the exact text the client sent, for responses
        std::string eventType;     // for events, such as "focus_changed"
        bool success;
        std::string error;
        nlohmann::json data;
    };

    // "sub --events focus_changed workspace_activated ...", for kWorkspaceEvents and the binding mode and tiling
    // direction events.
    std::string SubscribeCommand();

    // "command focus --workspace <name>". GlazeWM splits messages on whitespace with no quoting, so a name holding
    // whitespace or control characters could smuggle in extra arguments: those throw ProtocolError instead.
    std::string FocusWorkspaceCommand(std::string_view name);

    // "command focus --container-id <id>". Throws ProtocolError unless id is a UUID, the only form GlazeWM gives ids in.
    std::string FocusWindowCommand(std::string_view id);

    // "command <command>", for a command from the config written in GlazeWM's own syntax. Throws ProtocolError when it
    // is empty or holds a control character.
    std::string CommandMessage(std::string_view command);

    // Whether a response answers a command, whose failure is the command's own and leaves the connection usable.
    bool IsCommand(std::string_view clientMessage);

    // Throws ProtocolError when the text is not JSON or lacks the fields every server message has.
    ServerMessage ParseServerMessage(std::string_view text);

    // The workspaces in the data of a "query monitors" response, tagged with their monitor's HMONITOR.
    std::vector<Workspace> WorkspacesFromMonitors(const nlohmann::json& data);

    // The windows on each monitor's displayed workspace in the same data, in tree order through any split containers.
    std::vector<ManagedWindow> WindowsFromMonitors(const nlohmann::json& data);

    // The display names of the active binding modes in the data of a "query binding-modes" response.
    std::vector<std::wstring> BindingModesFromResponse(const nlohmann::json& data);

    // The direction in the data of a "query tiling-direction" response.
    TilingDirection TilingDirectionFromResponse(const nlohmann::json& data);

    bool ChangesWorkspaces(std::string_view eventType);
    // Focus moving to another container changes the direction shown, as does flipping it.
    bool ChangesTilingDirection(std::string_view eventType);

    // Keeps at most one query in flight: a burst of events costs one query, plus one more if events arrived while it
    // was out, instead of one per event.
    class QueryGate {
      public:
        // An event asks for fresh state. True means send the query now.
        bool RequestQuery();
        // The in-flight query was answered. True means events arrived meanwhile, so send another now.
        bool Answered();

      private:
        bool _inFlight = false;
        bool _stale = false;
    };

} // namespace glazewm

// Backend for GlazeWM over its IPC WebSocket. A receiver thread holds the connection and retries forever while GlazeWM
// is down, reporting Disconnected in the meantime. A sender queue delivers commands, so the UI thread never waits on
// the socket. The focused title comes from Windows events, since GlazeWM sends no event when a title changes.
class GlazeWm final : public IWindowManager {
  public:
    // port is GlazeWM's IPC port on 127.0.0.1, 6123 unless a test server stands in. focus follows the focused window,
    // or is null when nothing shows it, since its hooks wake the bar for every focus change and retitle.
    GlazeWm(ChangeSignal& changes, std::uint16_t port, std::unique_ptr<Native> focus);
    ~GlazeWm() override;
    GlazeWm(const GlazeWm&) = delete;
    GlazeWm& operator=(const GlazeWm&) = delete;

    std::optional<FocusedWindow> Focused() const override;
    std::vector<Workspace> Workspaces() const override;
    std::vector<ManagedWindow> Windows() const override;
    std::vector<std::wstring> BindingModes() const override;
    std::optional<TilingDirection> Direction() const override;
    WindowManagerStatus Status() const override;
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
        std::vector<Workspace> workspaces;
        std::vector<ManagedWindow> windows;
        std::vector<std::wstring> bindingModes;
        std::optional<TilingDirection> direction;
        WindowManagerStatus status;

        // No workspaces, windows, binding modes or direction yet.
        static Snapshot Empty(Connection connection, std::string detail);
        bool operator==(const Snapshot&) const = default;
    };

    void Receive();
    void Session();
    void SendCommand(const std::string& command);
    // Every send goes through here: WinHTTP fails a send while another one is pending.
    void SendSerialized(WebSocket& socket, std::string_view text);
    void Publish(const Snapshot& snapshot);
    // Queues a command built by makeCommand, which throws ProtocolError for an argument GlazeWM cannot take.
    void QueueCommand(const std::function<std::string()>& makeCommand);

    std::unique_ptr<Native> _focus;
    std::uint16_t _port;
    ChangeSignal& _changes;
    UniqueHandle _stopEvent;
    mutable std::mutex _mutex; // guards the members below it up to _sendMutex
    Snapshot _snapshot;
    bool _stopping = false;
    // Shared so the sender or the destructor can still use the socket while the session that opened it is ending.
    std::shared_ptr<WebSocket> _socket;
    std::mutex _sendMutex;
    // Held by pointer so the destructor can stop it, with no send pending, before it closes the socket.
    std::unique_ptr<WorkQueue<std::string>> _sender;
    // Declared last so every member above exists before the thread starts.
    std::thread _receiver;
};
