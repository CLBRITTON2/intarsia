#include "wm/GlazeWm.hpp"

#include "platform/Log.hpp"
#include "platform/PeerProcess.hpp"
#include "platform/Text.hpp"
#include "wm/GlazeWmProtocol.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

    constexpr INTERNET_PORT             kIpcPort = 6123;
    constexpr std::chrono::milliseconds kFirstRetry{2000};
    constexpr std::chrono::milliseconds kLongestRetry{10000};
    constexpr size_t                    kErrorExcerptBytes = 300;
    // Clicks faster than GlazeWM answers pile up here, and anything past this many is dropped with a warning.
    constexpr size_t  kMaxQueuedCommands = 16;
    constexpr wchar_t kName[]            = L"GlazeWM";
    constexpr wchar_t kImageName[]       = L"glazewm.exe";

} // namespace

GlazeWm::GlazeWm(HWND target, UINT changedMessage) :
    _native(target, changedMessage), _target(target), _changedMessage(changedMessage), _stopEvent(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")),
    _status{.name = kName, .connection = Connection::Disconnected, .detail = "not connected yet"}, _stopping(false),
    _sender(std::make_unique<WorkQueue<std::string>>(kMaxQueuedCommands, [this](const std::string& command) { SendCommand(command); })), _receiver([this] { Receive(); }) {}

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
    return _native.Focused();
}

std::vector<Workspace> GlazeWm::Workspaces() const {
    const std::scoped_lock lock(_mutex);
    return _workspaces;
}

WindowManagerStatus GlazeWm::Status() const {
    const std::scoped_lock lock(_mutex);
    return _status;
}

void GlazeWm::FocusWorkspace(const std::string& name) {
    std::string command;
    try {
        command = glazewm::FocusWorkspaceCommand(name);
    } catch (const glazewm::ProtocolError& error) {
        Log(LogLevel::Warning, "glazewm", "command_skipped", {{.key = "reason", .value = error.what()}});
        return;
    }
    if (!_sender->Push(command)) {
        Log(LogLevel::Warning, "glazewm", "command_dropped", {{.key = "reason", .value = "queue full"}, {.key = "command", .value = command}});
    }
}

// Retries forever, as the owner chose: restarting GlazeWM must not take the bar down, and the modules show that it is
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
                wasConnected = _status.connection == Connection::Connected;
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
            Publish({}, WindowManagerStatus{.name = kName, .connection = Connection::Disconnected, .detail = error.what()});
        }
        if (WaitForSingleObject(_stopEvent.get(), static_cast<DWORD>(retry.count())) == WAIT_OBJECT_0) {
            return;
        }
        retry = std::min(retry * 2, kLongestRetry);
    }
}

void GlazeWm::Session() {
    const auto socket = std::make_shared<WebSocket>(L"127.0.0.1", kIpcPort, L"/");
    // GlazeWM's IPC has no authentication, so whoever owns the listening port is checked before anything it says is
    // used: any process of any user could have bound 127.0.0.1:6123 while GlazeWM was down.
    RequireSameUserProcess(LoopbackPeerProcess(socket->LocalPort(), kIpcPort), kImageName);

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

    glazewm::QueryGate gate;
    SendSerialized(*socket, glazewm::SubscribeCommand());
    if (gate.RequestQuery()) {
        SendSerialized(*socket, glazewm::kQueryMonitors);
    }
    while (true) {
        const glazewm::ServerMessage message = glazewm::ParseServerMessage(socket->Receive());
        if (!message.success) {
            // A rejected command (a workspace that closed just before the click) is that command's failure alone.
            if (message.kind == glazewm::MessageKind::Response && glazewm::IsCommand(message.clientMessage)) {
                Log(LogLevel::Warning, "glazewm", "command_failed", {{.key = "command", .value = message.clientMessage}, {.key = "reason", .value = message.error}});
                continue;
            }
            throw glazewm::ProtocolError(
                std::format("GlazeWM rejected \"{}\": {}", LogExcerpt(message.clientMessage, kErrorExcerptBytes), LogExcerpt(message.error, kErrorExcerptBytes)));
        }
        if (message.kind == glazewm::MessageKind::Event && glazewm::ChangesWorkspaces(message.eventType)) {
            if (gate.RequestQuery()) {
                SendSerialized(*socket, glazewm::kQueryMonitors);
            }
        } else if (message.kind == glazewm::MessageKind::Response && message.clientMessage == glazewm::kQueryMonitors) {
            Publish(glazewm::WorkspacesFromMonitors(message.data), WindowManagerStatus{.name = kName, .connection = Connection::Connected, .detail = ""});
            if (gate.Answered()) {
                SendSerialized(*socket, glazewm::kQueryMonitors);
            }
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

// Posts only when something changed: most focus events move focus within a workspace and leave the list as it was.
void GlazeWm::Publish(std::vector<Workspace> workspaces, WindowManagerStatus status) {
    {
        const std::scoped_lock lock(_mutex);
        if (workspaces == _workspaces && status == _status) {
            return;
        }
        _workspaces = std::move(workspaces);
        _status     = std::move(status);
    }
    if (!PostMessageW(_target, _changedMessage, 0, 0)) {
        Log(LogLevel::Warning, "glazewm", "post_failed", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}
