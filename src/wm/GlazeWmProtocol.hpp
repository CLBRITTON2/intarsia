#pragma once

#include "wm/IWindowManager.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// GlazeWM's IPC: text commands such as "query monitors" over a WebSocket on 127.0.0.1:6123, answered with JSON.
// Shapes follow glzr-io/glazewm packages/wm-common (ipc.rs, wm_event.rs, dtos/), checked against v3.10.1.
// Everything the server sends is untrusted: errors quote at most a short escaped excerpt of it.
namespace glazewm {

    inline constexpr std::string_view kQueryMonitors         = "query monitors";
    inline constexpr std::string_view kQueryBindingModes     = "query binding-modes";
    inline constexpr std::string_view kQueryTilingDirection  = "query tiling-direction";
    inline constexpr std::string_view kToggleTilingDirection = "command toggle-tiling-direction";

    inline constexpr std::string_view kBindingModesChanged    = "binding_modes_changed";
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
        MessageKind    kind;
        std::string    clientMessage; // the exact text the client sent, for responses
        std::string    eventType;     // for events, such as "focus_changed"
        bool           success;
        std::string    error;
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

    bool            ChangesWorkspaces(std::string_view eventType);
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
        bool _stale    = false;
    };

} // namespace glazewm
