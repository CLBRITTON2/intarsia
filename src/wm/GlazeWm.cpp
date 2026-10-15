#include "wm/GlazeWm.hpp"

#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/WebSocket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

    constexpr std::chrono::milliseconds kFirstRetry{2000};
    constexpr std::chrono::milliseconds kLongestRetry{10000};
    constexpr size_t                    kErrorExcerptBytes = 300;
    // Clicks faster than GlazeWM answers pile up here, and anything past this many is dropped with a warning.
    constexpr size_t  kMaxQueuedCommands = 16;
    constexpr wchar_t kName[]            = L"GlazeWM";
    constexpr wchar_t kImageName[]       = L"glazewm.exe";

} // namespace

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
                if (dash && character != '-') {
                    return false;
                }
                if (!dash && !hex) {
                    return false;
                }
            }
            return true;
        }

        // The display name when there is a non-empty one, else the name: workspaces and binding modes both have both.
        std::wstring DisplayLabel(const nlohmann::json& object, std::string_view context) {
            const nlohmann::json& displayName = Field(object, "displayName", context);
            if (displayName.is_string() && !displayName.get<std::string>().empty()) {
                return DrawableText(displayName.get<std::string>());
            }
            return DrawableText(StringField(object, "name", context));
        }

        // Empty unless the message carries an error string.
        std::string ErrorText(const nlohmann::json& message) {
            const auto error = message.find("error");
            if (error != message.end() && error->is_string()) {
                return error->get<std::string>();
            }
            return {};
        }

        // Empty when the event's data is not an object.
        std::string EventType(const nlohmann::json& data) {
            if (!data.is_object()) {
                return {};
            }
            return StringField(data, "eventType", "event");
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
        std::string       errorText   = ErrorText(message);
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
            return ServerMessage{
                .kind          = MessageKind::Event,
                .clientMessage = "",
                .eventType     = EventType(data),
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

GlazeWm::Snapshot GlazeWm::Snapshot::Empty(Connection connection, std::string detail) {
    return Snapshot{
        .workspaces   = {},
        .windows      = {},
        .bindingModes = {},
        .direction    = std::nullopt,
        .status =
            WindowManagerStatus{
                .name       = kName,
                .connection = connection,
                .detail     = std::move(detail),
            },
    };
}

GlazeWm::GlazeWm(ChangeSignal& changes, std::uint16_t port, std::unique_ptr<Native> focus) :
    _focus(std::move(focus)), _port(port), _changes(changes), _stopEvent(ManualResetEvent("stop")), _snapshot(Snapshot::Empty(Connection::Disconnected, "not connected yet")),
    _sender(std::make_unique<WorkQueue<std::string>>(kMaxQueuedCommands,
                                                     [this] { return WorkQueue<std::string>::Process([this](const std::string& command) { SendCommand(command); }); })),
    _receiver([this] { Receive(); }) {}

// Stops the sender first so no send is pending, then closes the socket: WinHttpWebSocketClose cancels the receiver's
// pending receive, which is the documented way to unblock it.
GlazeWm::~GlazeWm() {
    std::shared_ptr<WebSocket> socket;
    {
        const std::scoped_lock lock(_mutex);
        _stopping = true;
        socket    = _socket;
    }
    SetEvent(_stopEvent.get());
    _sender.reset();
    if (socket != nullptr) {
        const std::scoped_lock sending(_sendMutex);
        try {
            socket->Close();
        } catch (const std::exception& error) {
            // Every WinHttpWebSocketClose failure means the connection is already gone, so the receiver returns anyway.
            Log(LogLevel::Warning, "glazewm", "close_failed", {{.key = "reason", .value = error.what()}});
        }
    }
    _receiver.join();
}

std::optional<FocusedWindow> GlazeWm::Focused() const {
    if (_focus == nullptr) {
        return std::nullopt;
    }
    return _focus->Focused();
}

std::vector<Workspace> GlazeWm::Workspaces() const {
    const std::scoped_lock lock(_mutex);
    return _snapshot.workspaces;
}

std::vector<ManagedWindow> GlazeWm::Windows() const {
    const std::scoped_lock lock(_mutex);
    return _snapshot.windows;
}

std::vector<std::wstring> GlazeWm::BindingModes() const {
    const std::scoped_lock lock(_mutex);
    return _snapshot.bindingModes;
}

std::optional<TilingDirection> GlazeWm::Direction() const {
    const std::scoped_lock lock(_mutex);
    return _snapshot.direction;
}

WindowManagerStatus GlazeWm::Status() const {
    const std::scoped_lock lock(_mutex);
    return _snapshot.status;
}

void GlazeWm::FocusWorkspace(const std::string& name) {
    QueueCommand([&name] { return glazewm::FocusWorkspaceCommand(name); });
}

void GlazeWm::FocusWindow(const std::string& id) {
    QueueCommand([&id] { return glazewm::FocusWindowCommand(id); });
}

void GlazeWm::ToggleTilingDirection() {
    QueueCommand([] { return std::string(glazewm::kToggleTilingDirection); });
}

void GlazeWm::RunCommand(const std::string& command) {
    QueueCommand([&command] { return glazewm::CommandMessage(command); });
}

void GlazeWm::QueueCommand(const std::function<std::string()>& makeCommand) {
    std::string command;
    try {
        command = makeCommand();
    } catch (const glazewm::ProtocolError& error) {
        Log(LogLevel::Warning, "glazewm", "command_skipped", {{.key = "reason", .value = error.what()}});
        return;
    }
    if (!_sender->Push(command)) {
        Log(LogLevel::Warning, "glazewm", "command_dropped", {{.key = "reason", .value = "queue full"}, {.key = "command", .value = command}});
    }
}

// Retries forever: restarting GlazeWM must not take the bar down, and the modules show that it is
// disconnected until it is back. Only the change from up to down is logged, and the wait backs off while it stays down.
void GlazeWm::Receive() {
    std::chrono::milliseconds retry     = kFirstRetry;
    bool                      loggedOut = false;
    while (true) {
        try {
            Session();
        } catch (const std::exception& error) {
            bool wasConnected = false;
            {
                const std::scoped_lock lock(_mutex);
                if (_stopping) {
                    return;
                }
                wasConnected = _snapshot.status.connection == Connection::Connected;
            }
            if (wasConnected) {
                retry     = kFirstRetry;
                loggedOut = false;
            }
            if (!loggedOut) {
                Log(LogLevel::Warning, "glazewm", "disconnected",
                    {{.key = "reason", .value = error.what()},
                     {.key = "retry_ms", .value = std::to_string(retry.count())},
                     {.key = "max_retry_ms", .value = std::to_string(kLongestRetry.count())}});
                loggedOut = true;
            }
            Publish(Snapshot::Empty(Connection::Disconnected, error.what()));
        }
        if (WaitForSingleObject(_stopEvent.get(), static_cast<DWORD>(retry.count())) == WAIT_OBJECT_0) {
            return;
        }
        retry = std::min(retry * 2, kLongestRetry);
    }
}

void GlazeWm::Session() {
    const auto socket = std::make_shared<WebSocket>(L"127.0.0.1", _port, L"/");
    // GlazeWM's IPC has no authentication, so whoever owns the listening port is checked before anything it says is
    // used: any process of any user could have bound 127.0.0.1:6123 while GlazeWM was down.
    RequireSameUserProcess(LoopbackPeerProcess(socket->LocalPort(), _port), kImageName);

    // Published for the life of this connection so the sender and the destructor can reach it.
    struct Registration {
        GlazeWm& owner;
        Registration(GlazeWm& manager, const std::shared_ptr<WebSocket>& active) : owner(manager) {
            const std::scoped_lock lock(owner._mutex);
            if (owner._stopping) {
                throw std::runtime_error("stopping");
            }
            owner._socket = active;
        }
        ~Registration() {
            const std::scoped_lock lock(owner._mutex);
            owner._socket = nullptr;
        }
        Registration(const Registration&)            = delete;
        Registration& operator=(const Registration&) = delete;
    };
    const Registration registration(*this, socket);
    Log(LogLevel::Info, "glazewm", "connected", {});

    glazewm::QueryGate monitorsGate;
    glazewm::QueryGate bindingModesGate;
    glazewm::QueryGate directionGate;
    const auto         ask = [this, &socket](glazewm::QueryGate& gate, std::string_view query) {
        if (gate.RequestQuery()) {
            SendSerialized(*socket, query);
        }
    };
    const auto answered = [this, &socket](glazewm::QueryGate& gate, std::string_view query) {
        if (gate.Answered()) {
            SendSerialized(*socket, query);
        }
    };

    Snapshot current = Snapshot::Empty(Connection::Connected, "");
    SendSerialized(*socket, glazewm::SubscribeCommand());
    ask(monitorsGate, glazewm::kQueryMonitors);
    ask(bindingModesGate, glazewm::kQueryBindingModes);
    ask(directionGate, glazewm::kQueryTilingDirection);
    while (true) {
        const glazewm::ServerMessage message  = glazewm::ParseServerMessage(socket->Receive());
        const bool                   response = message.kind == glazewm::MessageKind::Response;
        if (!message.success) {
            // A rejected command (a workspace that closed just before the click) is that command's failure alone.
            if (response && glazewm::IsCommand(message.clientMessage)) {
                Log(LogLevel::Warning, "glazewm", "command_failed", {{.key = "command", .value = message.clientMessage}, {.key = "reason", .value = message.error}});
                continue;
            }
            // GlazeWM answers "No direction container." while nothing focused has a direction: there is none to show.
            if (response && message.clientMessage == glazewm::kQueryTilingDirection) {
                current.direction = std::nullopt;
                Publish(current);
                answered(directionGate, glazewm::kQueryTilingDirection);
                continue;
            }
            throw glazewm::ProtocolError(
                std::format("GlazeWM rejected \"{}\": {}", LogExcerpt(message.clientMessage, kErrorExcerptBytes), LogExcerpt(message.error, kErrorExcerptBytes)));
        }
        if (!response) {
            if (glazewm::ChangesWorkspaces(message.eventType)) {
                ask(monitorsGate, glazewm::kQueryMonitors);
            }
            if (message.eventType == glazewm::kBindingModesChanged) {
                ask(bindingModesGate, glazewm::kQueryBindingModes);
            }
            if (glazewm::ChangesTilingDirection(message.eventType)) {
                ask(directionGate, glazewm::kQueryTilingDirection);
            }
        } else if (message.clientMessage == glazewm::kQueryMonitors) {
            current.workspaces = glazewm::WorkspacesFromMonitors(message.data);
            current.windows    = glazewm::WindowsFromMonitors(message.data);
            Publish(current);
            answered(monitorsGate, glazewm::kQueryMonitors);
        } else if (message.clientMessage == glazewm::kQueryBindingModes) {
            current.bindingModes = glazewm::BindingModesFromResponse(message.data);
            Publish(current);
            answered(bindingModesGate, glazewm::kQueryBindingModes);
        } else if (message.clientMessage == glazewm::kQueryTilingDirection) {
            current.direction = glazewm::TilingDirectionFromResponse(message.data);
            Publish(current);
            answered(directionGate, glazewm::kQueryTilingDirection);
        }
    }
}

// Runs on the sender's queue thread, so it logs instead of throwing.
void GlazeWm::SendCommand(const std::string& command) {
    std::shared_ptr<WebSocket> socket;
    {
        const std::scoped_lock lock(_mutex);
        socket = _socket;
    }
    if (socket == nullptr) {
        Log(LogLevel::Warning, "glazewm", "command_dropped", {{.key = "reason", .value = "not connected"}, {.key = "command", .value = command}});
        return;
    }
    try {
        SendSerialized(*socket, command);
    } catch (const std::exception& error) {
        // The receiver sees the same broken connection and reconnects, so this only reports the lost command.
        Log(LogLevel::Warning, "glazewm", "command_dropped", {{.key = "reason", .value = error.what()}, {.key = "command", .value = command}});
    }
}

void GlazeWm::SendSerialized(WebSocket& socket, std::string_view text) {
    const std::scoped_lock lock(_sendMutex);
    socket.Send(text);
}

// Posts only when something changed: many events, such as a window resized, leave what the bar shows as it was.
void GlazeWm::Publish(const Snapshot& snapshot) {
    {
        const std::scoped_lock lock(_mutex);
        if (snapshot == _snapshot) {
            return;
        }
        _snapshot = snapshot;
    }
    _changes.Notify("glazewm");
}
