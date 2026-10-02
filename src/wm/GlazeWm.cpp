#include "wm/GlazeWm.hpp"

#include "platform/Log.hpp"
#include "platform/PeerProcess.hpp"
#include "platform/Text.hpp"
#include "wm/GlazeWmProtocol.hpp"

#include <cstdint>
#include <algorithm>
#include <chrono>
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

GlazeWm::Snapshot GlazeWm::Snapshot::Empty(Connection connection, std::string detail) {
    return Snapshot{.workspaces   = {},
                    .windows      = {},
                    .bindingModes = {},
                    .direction    = std::nullopt,
                    .status       = WindowManagerStatus{.name = kName, .connection = connection, .detail = std::move(detail)}};
}

GlazeWm::GlazeWm(ChangeSignal& changes, std::uint16_t port, std::unique_ptr<Native> focus) :
    _focus(std::move(focus)), _port(port), _changes(changes), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")),
    _snapshot(Snapshot::Empty(Connection::Disconnected, "not connected yet")),
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
    return _focus != nullptr ? _focus->Focused() : std::nullopt;
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
