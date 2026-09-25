#pragma once

#include "wm/IWindowManager.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// GlazeWM's IPC: text commands such as "query monitors" over a WebSocket on 127.0.0.1:6123, answered with JSON.
// Shapes follow glzr-io/glazewm packages/wm-common (ipc.rs, wm_event.rs, dtos/), checked against v3.10.1.
// Everything the server sends is untrusted: errors quote at most a short escaped excerpt of it.
namespace glazewm {

    inline constexpr std::string_view kQueryMonitors = "query monitors";

    // The events that can change the workspace list or which workspace is focused. Subscribing to these alone
    // skips noisy ones like user_config_changed, which carries GlazeWM's whole config.
    inline constexpr std::array<std::string_view, 9> kWorkspaceEvents{"focus_changed",   "workspace_activated", "workspace_deactivated", "workspace_updated", "monitor_added",
                                                                      "monitor_removed", "monitor_updated",     "window_managed",        "window_unmanaged"};

    class ProtocolError : public std::runtime_error {
      public:
        using std::runtime_error::runtime_error;
    };

    enum class MessageKind {
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

    // "sub --events focus_changed workspace_activated ...", for kWorkspaceEvents.
    std::string SubscribeCommand();

    // "command focus --workspace <name>". GlazeWM splits messages on whitespace with no quoting, so a name holding
    // whitespace or control characters could smuggle in extra arguments: those throw ProtocolError instead.
    std::string FocusWorkspaceCommand(std::string_view name);

    // Whether a response answers a command, whose failure is the command's own and leaves the connection usable.
    bool IsCommand(std::string_view clientMessage);

    // Throws ProtocolError when the text is not JSON or lacks the fields every server message has.
    ServerMessage ParseServerMessage(std::string_view text);

    // The workspaces in the data of a "query monitors" response, tagged with their monitor's HMONITOR.
    std::vector<Workspace> WorkspacesFromMonitors(const nlohmann::json& data);

    bool                   ChangesWorkspaces(std::string_view eventType);

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
