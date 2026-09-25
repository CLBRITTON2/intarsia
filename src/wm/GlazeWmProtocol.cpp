#include "wm/GlazeWmProtocol.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <format>

namespace glazewm {

    namespace {

        constexpr size_t      kExcerptBytes = 200;

        const nlohmann::json& Field(const nlohmann::json& object, std::string_view key, std::string_view context) {
            const auto found = object.find(key);
            if (found == object.end()) {
                throw ProtocolError(std::format("GlazeWM {} has no '{}' field", context, key));
            }
            return *found;
        }

        std::string StringField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            const nlohmann::json& value = Field(object, key, context);
            if (!value.is_string()) {
                throw ProtocolError(std::format("GlazeWM {} field '{}' is a {}, expected a string", context, key, value.type_name()));
            }
            return value.get<std::string>();
        }

        bool BoolField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            const nlohmann::json& value = Field(object, key, context);
            if (!value.is_boolean()) {
                throw ProtocolError(std::format("GlazeWM {} field '{}' is a {}, expected a boolean", context, key, value.type_name()));
            }
            return value.get<bool>();
        }

        const nlohmann::json& ArrayField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            const nlohmann::json& value = Field(object, key, context);
            if (!value.is_array()) {
                throw ProtocolError(std::format("GlazeWM {} field '{}' is a {}, expected an array", context, key, value.type_name()));
            }
            return value;
        }

        Workspace ReadWorkspace(const nlohmann::json& workspace, std::intptr_t monitor) {
            const nlohmann::json& displayName = Field(workspace, "displayName", "workspace");
            const std::string     label =
                displayName.is_string() && !displayName.get<std::string>().empty() ? displayName.get<std::string>() : StringField(workspace, "name", "workspace");
            return Workspace{
                .label = ToWide(label), .name = StringField(workspace, "name", "workspace"), .focused = BoolField(workspace, "hasFocus", "workspace"), .monitor = monitor};
        }

    } // namespace

    std::string SubscribeCommand() {
        std::string command = "sub --events";
        for (const std::string_view event : kWorkspaceEvents) {
            command += ' ';
            command += event;
        }
        return command;
    }

    std::string FocusWorkspaceCommand(std::string_view name) {
        const bool unsafe = std::ranges::any_of(name, [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte <= 0x20u || byte == 0x7Fu;
        });
        if (name.empty() || unsafe) {
            throw ProtocolError(
                std::format("workspace name \"{}\" cannot be sent to GlazeWM: it is empty or holds whitespace or control characters", LogExcerpt(name, kExcerptBytes)));
        }
        return std::format("command focus --workspace {}", name);
    }

    bool IsCommand(std::string_view clientMessage) {
        return clientMessage.starts_with("command ");
    }

    ServerMessage ParseServerMessage(std::string_view text) {
        nlohmann::json message = nlohmann::json::parse(text, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            throw ProtocolError(std::format("GlazeWM sent a message that is not a JSON object: \"{}\"", LogExcerpt(text, kExcerptBytes)));
        }

        const std::string messageType = StringField(message, "messageType", "message");
        const bool        success     = BoolField(message, "success", "message");
        const auto        error       = message.find("error");
        std::string       errorText   = error != message.end() && error->is_string() ? error->get<std::string>() : std::string();
        nlohmann::json    data        = message.contains("data") ? message["data"] : nlohmann::json();

        if (messageType == "client_response") {
            return ServerMessage{.kind          = MessageKind::Response,
                                 .clientMessage = StringField(message, "clientMessage", "response"),
                                 .eventType     = "",
                                 .success       = success,
                                 .error         = std::move(errorText),
                                 .data          = std::move(data)};
        }
        if (messageType == "event_subscription") {
            const std::string eventType = data.is_object() ? StringField(data, "eventType", "event") : std::string();
            return ServerMessage{
                .kind = MessageKind::Event, .clientMessage = "", .eventType = eventType, .success = success, .error = std::move(errorText), .data = std::move(data)};
        }
        throw ProtocolError(std::format("GlazeWM sent an unknown messageType \"{}\"", LogExcerpt(messageType, kExcerptBytes)));
    }

    std::vector<Workspace> WorkspacesFromMonitors(const nlohmann::json& data) {
        std::vector<Workspace> workspaces;
        for (const nlohmann::json& monitor : ArrayField(data, "monitors", "monitors response")) {
            const nlohmann::json& handle = Field(monitor, "handle", "monitor");
            if (!handle.is_number_integer()) {
                throw ProtocolError(std::format("GlazeWM monitor handle is a {}, expected an integer", handle.type_name()));
            }
            const auto monitorHandle = handle.get<std::intptr_t>();
            for (const nlohmann::json& child : ArrayField(monitor, "children", "monitor")) {
                if (StringField(child, "type", "monitor child") == "workspace") {
                    workspaces.push_back(ReadWorkspace(child, monitorHandle));
                }
            }
        }
        return workspaces;
    }

    bool ChangesWorkspaces(std::string_view eventType) {
        return std::ranges::find(kWorkspaceEvents, eventType) != kWorkspaceEvents.end();
    }

    bool QueryGate::RequestQuery() {
        if (_inFlight) {
            _stale = true;
            return false;
        }
        _inFlight = true;
        return true;
    }

    bool QueryGate::Answered() {
        _inFlight = _stale;
        _stale    = false;
        return _inFlight;
    }

} // namespace glazewm
