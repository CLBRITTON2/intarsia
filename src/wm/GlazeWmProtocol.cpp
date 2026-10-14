#include "wm/GlazeWmProtocol.hpp"

#include "platform/Text.hpp"

#include <cstdint>
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

        // expected names the type isType checks for, as "a string".
        const nlohmann::json& TypedField(const nlohmann::json& object, std::string_view key, std::string_view context, bool (nlohmann::json::*isType)() const noexcept,
                                         std::string_view expected) {
            const nlohmann::json& value = Field(object, key, context);
            if (!(value.*isType)()) {
                throw ProtocolError(std::format("GlazeWM {} field '{}' is a {}, expected {}", context, key, value.type_name(), expected));
            }
            return value;
        }

        std::string StringField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            return TypedField(object, key, context, &nlohmann::json::is_string, "a string").get<std::string>();
        }

        bool BoolField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            return TypedField(object, key, context, &nlohmann::json::is_boolean, "a boolean").get<bool>();
        }

        const nlohmann::json& ArrayField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            return TypedField(object, key, context, &nlohmann::json::is_array, "an array");
        }

        // A monitor's HMONITOR or a window's HWND.
        std::intptr_t HandleField(const nlohmann::json& object, std::string_view key, std::string_view context) {
            return TypedField(object, key, context, &nlohmann::json::is_number_integer, "an integer").get<std::intptr_t>();
        }

        // Last child on top, so the walk pops them in tree order.
        void PushChildren(std::vector<const nlohmann::json*>& pending, const nlohmann::json& container, std::string_view context) {
            const nlohmann::json& children = ArrayField(container, "children", context);
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                pending.push_back(&*child);
            }
        }

        bool IsUuid(std::string_view id) {
            if (id.size() != 36) {
                return false;
            }
            for (size_t index = 0; index < id.size(); ++index) {
                const char character = id[index];
                const bool dash      = index == 8 || index == 13 || index == 18 || index == 23;
                const bool hex       = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
                if (dash ? character != '-' : !hex) {
                    return false;
                }
            }
            return true;
        }

        // The display name when there is a non-empty one, else the name: workspaces and binding modes both have both.
        std::wstring DisplayLabel(const nlohmann::json& object, std::string_view context) {
            const nlohmann::json& displayName = Field(object, "displayName", context);
            return DrawableText(displayName.is_string() && !displayName.get<std::string>().empty() ? displayName.get<std::string>() : StringField(object, "name", context));
        }

        Workspace ReadWorkspace(const nlohmann::json& workspace, std::intptr_t monitor) {
            // A workspace's children are its windows and the split containers holding them, so any child means a window.
            return Workspace{
                .label    = DisplayLabel(workspace, "workspace"),
                .name     = StringField(workspace, "name", "workspace"),
                .focused  = BoolField(workspace, "hasFocus", "workspace"),
                .occupied = !ArrayField(workspace, "children", "workspace").empty(),
                .monitor  = monitor,
            };
        }

    } // namespace

    std::string SubscribeCommand() {
        std::string command = "sub --events";
        for (const std::string_view event : kWorkspaceEvents) {
            command += ' ';
            command += event;
        }
        for (const std::string_view event : {kBindingModesChanged, kTilingDirectionChanged}) {
            command += ' ';
            command += event;
        }
        return command;
    }

    std::string FocusWorkspaceCommand(std::string_view name) {
        const bool unsafe = std::ranges::any_of(name, [](char character) { return character == ' ' || IsControlByte(character); });
        if (name.empty() || unsafe) {
            throw ProtocolError(
                std::format("workspace name \"{}\" cannot be sent to GlazeWM: it is empty or holds whitespace or control characters", LogExcerpt(name, kExcerptBytes)));
        }
        return std::format("command focus --workspace {}", name);
    }

    std::string FocusWindowCommand(std::string_view id) {
        if (!IsUuid(id)) {
            throw ProtocolError(std::format("window id \"{}\" cannot be sent to GlazeWM: it is not a UUID", LogExcerpt(id, kExcerptBytes)));
        }
        return std::format("command focus --container-id {}", id);
    }

    std::string CommandMessage(std::string_view command) {
        const bool control = std::ranges::any_of(command, IsControlByte);
        if (command.empty() || control) {
            throw ProtocolError(std::format("command \"{}\" cannot be sent to GlazeWM: it is empty or holds control characters", LogExcerpt(command, kExcerptBytes)));
        }
        return std::format("command {}", command);
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
        // Moved, never copied: nlohmann copies recursively, so a peer's deeply nested data would overflow the stack.
        const auto     dataField = message.find("data");
        nlohmann::json data      = dataField != message.end() ? std::move(*dataField) : nlohmann::json();

        if (messageType == "client_response") {
            return ServerMessage{
                .kind          = MessageKind::Response,
                .clientMessage = StringField(message, "clientMessage", "response"),
                .eventType     = "",
                .success       = success,
                .error         = std::move(errorText),
                .data          = std::move(data),
            };
        }
        if (messageType == "event_subscription") {
            const std::string eventType = data.is_object() ? StringField(data, "eventType", "event") : std::string();
            return ServerMessage{
                .kind          = MessageKind::Event,
                .clientMessage = "",
                .eventType     = eventType,
                .success       = success,
                .error         = std::move(errorText),
                .data          = std::move(data),
            };
        }
        throw ProtocolError(std::format("GlazeWM sent an unknown messageType \"{}\"", LogExcerpt(messageType, kExcerptBytes)));
    }

    std::vector<Workspace> WorkspacesFromMonitors(const nlohmann::json& data) {
        std::vector<Workspace> workspaces;
        for (const nlohmann::json& monitor : ArrayField(data, "monitors", "monitors response")) {
            const std::intptr_t monitorHandle = HandleField(monitor, "handle", "monitor");
            for (const nlohmann::json& child : ArrayField(monitor, "children", "monitor")) {
                if (StringField(child, "type", "monitor child") == "workspace") {
                    workspaces.push_back(ReadWorkspace(child, monitorHandle));
                }
            }
        }
        return workspaces;
    }

    std::vector<ManagedWindow> WindowsFromMonitors(const nlohmann::json& data) {
        std::vector<ManagedWindow> windows;
        for (const nlohmann::json& monitor : ArrayField(data, "monitors", "monitors response")) {
            const std::intptr_t monitorHandle = HandleField(monitor, "handle", "monitor");
            for (const nlohmann::json& child : ArrayField(monitor, "children", "monitor")) {
                if (StringField(child, "type", "monitor child") != "workspace" || !BoolField(child, "isDisplayed", "workspace")) {
                    continue;
                }
                // A stack of its own rather than recursion: the tree's depth is the peer's to choose.
                std::vector<const nlohmann::json*> pending;
                PushChildren(pending, child, "workspace");
                while (!pending.empty()) {
                    const nlohmann::json& node = *pending.back();
                    pending.pop_back();
                    const std::string type = StringField(node, "type", "workspace child");
                    if (type == "window") {
                        windows.push_back(ManagedWindow{
                            .id      = StringField(node, "id", "window"),
                            .handle  = HandleField(node, "handle", "window"),
                            .focused = BoolField(node, "hasFocus", "window"),
                            .monitor = monitorHandle,
                        });
                    } else if (type == "split") {
                        PushChildren(pending, node, "split");
                    } else {
                        throw ProtocolError(std::format("GlazeWM workspace holds a \"{}\" container, expected split or window", LogExcerpt(type, kExcerptBytes)));
                    }
                }
            }
        }
        return windows;
    }

    std::vector<std::wstring> BindingModesFromResponse(const nlohmann::json& data) {
        std::vector<std::wstring> modes;
        for (const nlohmann::json& mode : ArrayField(data, "bindingModes", "binding modes response")) {
            modes.push_back(DisplayLabel(mode, "binding mode"));
        }
        return modes;
    }

    TilingDirection TilingDirectionFromResponse(const nlohmann::json& data) {
        const std::string direction = StringField(data, "tilingDirection", "tiling direction response");
        if (direction == "horizontal") {
            return TilingDirection::Horizontal;
        }
        if (direction == "vertical") {
            return TilingDirection::Vertical;
        }
        throw ProtocolError(std::format("GlazeWM sent an unknown tiling direction \"{}\"", LogExcerpt(direction, kExcerptBytes)));
    }

    bool ChangesWorkspaces(std::string_view eventType) {
        return std::ranges::find(kWorkspaceEvents, eventType) != kWorkspaceEvents.end();
    }

    bool ChangesTilingDirection(std::string_view eventType) {
        return eventType == kTilingDirectionChanged || eventType == "focus_changed" || eventType == "focused_container_moved";
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
