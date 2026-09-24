#include "wm/GlazeWm.hpp"

#include "platform/PeerProcess.hpp"
#include "platform/Text.hpp"
#include "wm/GlazeWmProtocol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

    constexpr INTERNET_PORT             kIpcPort = 6123;
    constexpr std::chrono::milliseconds kFirstRetry{2000};
    constexpr std::chrono::milliseconds kLongestRetry{10000};
    constexpr size_t                    kLogExcerptBytes = 300;
    // Clicks faster than GlazeWM answers pile up here, and anything past this many is dropped with a warning.
    constexpr size_t  kMaxQueuedCommands = 16;
    constexpr wchar_t kName[]            = L"GlazeWM";
    constexpr wchar_t kImageName[]       = L"glazewm.exe";

    void              LogWarning(std::string_view event, std::string_view reason) {
        std::fprintf(stderr, "intarsia: warning component=glazewm event=%.*s reason=\"%s\"\n", static_cast<int>(event.size()), event.data(),
                                  LogExcerpt(reason, kLogExcerptBytes).c_str());
    }

} // namespace

GlazeWm::GlazeWm(HWND target, UINT changedMessage) :
    native_(target, changedMessage), target_(target), changedMessage_(changedMessage), stopEvent_(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")),
    commandEvent_(OwnHandle(CreateEventW(nullptr, FALSE, FALSE, nullptr), "CreateEventW(command)")),
    status_{.name = kName, .connection = Connection::Disconnected, .detail = "not connected yet"}, stopping_(false), receiver_([this] { Receive(); }),
    sender_([this] { SendCommands(); }) {}

// Stops the sender first so no send is pending, then closes the socket: WinHttpWebSocketClose cancels the receiver's
// pending receive, which is the documented way to unblock it.
GlazeWm::~GlazeWm() {
    std::shared_ptr<WebSocket> socket;
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        socket    = socket_;
    }
    SetEvent(stopEvent_.get());
    sender_.join();
    if (socket != nullptr) {
        const std::scoped_lock sending(sendMutex_);
        try {
            socket->Close();
        } catch (const std::exception& error) {
            // Every WinHttpWebSocketClose failure means the connection is already gone, so the receiver returns anyway.
            LogWarning("close_failed", error.what());
        }
    }
    receiver_.join();
}

std::optional<FocusedWindow> GlazeWm::Focused() const {
    return native_.Focused();
}

std::vector<Workspace> GlazeWm::Workspaces() const {
    const std::scoped_lock lock(mutex_);
    return workspaces_;
}

WindowManagerStatus GlazeWm::Status() const {
    const std::scoped_lock lock(mutex_);
    return status_;
}

void GlazeWm::FocusWorkspace(const std::string& name) {
    std::string command;
    try {
        command = glazewm::FocusWorkspaceCommand(name);
    } catch (const glazewm::ProtocolError& error) {
        LogWarning("command_skipped", error.what());
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        if (commands_.size() >= kMaxQueuedCommands) {
            LogWarning("command_dropped", "the command queue is full: " + command);
            return;
        }
        commands_.push_back(std::move(command));
    }
    SetEvent(commandEvent_.get());
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
                const std::scoped_lock lock(mutex_);
                if (stopping_) {
                    return;
                }
                wasConnected = status_.connection == Connection::Connected;
            }
            if (wasConnected) {
                retry     = kFirstRetry;
                loggedOut = false;
            }
            if (!loggedOut) {
                LogWarning("disconnected", std::format("{} (retrying every {} ms, backing off to {} ms)", error.what(), retry.count(), kLongestRetry.count()));
                loggedOut = true;
            }
            Publish({}, WindowManagerStatus{.name = kName, .connection = Connection::Disconnected, .detail = error.what()});
        }
        if (WaitForSingleObject(stopEvent_.get(), static_cast<DWORD>(retry.count())) == WAIT_OBJECT_0) {
            return;
        }
        retry = std::min(retry * 2, kLongestRetry);
    }
}

void GlazeWm::Session() {
    const auto socket = std::make_shared<WebSocket>(L"127.0.0.1", kIpcPort, L"/");
    // GlazeWM's IPC has no authentication, so whoever owns the listening port is checked before anything it says is
    // used: any process of any user could have bound 127.0.0.1:6123 while GlazeWM was down.
    RequireSameUserProcess(LoopbackListenerProcess(kIpcPort), kImageName);

    // Published for the life of this connection so the sender and the destructor can reach it.
    struct Registration {
        GlazeWm& owner;
        Registration(GlazeWm& manager, const std::shared_ptr<WebSocket>& active) : owner(manager) {
            const std::scoped_lock lock(owner.mutex_);
            if (owner.stopping_) {
                throw std::runtime_error("stopping");
            }
            owner.socket_ = active;
        }
        ~Registration() {
            const std::scoped_lock lock(owner.mutex_);
            owner.socket_ = nullptr;
        }
        Registration(const Registration&)            = delete;
        Registration& operator=(const Registration&) = delete;
    };
    const Registration registration(*this, socket);
    std::fprintf(stderr, "intarsia: info component=glazewm event=connected\n");

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
                LogWarning("command_failed", std::format("{}: {}", message.clientMessage, message.error));
                continue;
            }
            throw glazewm::ProtocolError(
                std::format("GlazeWM rejected \"{}\": {}", LogExcerpt(message.clientMessage, kLogExcerptBytes), LogExcerpt(message.error, kLogExcerptBytes)));
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

void GlazeWm::SendCommands() {
    const std::array<HANDLE, 2> waits{stopEvent_.get(), commandEvent_.get()};
    while (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        std::deque<std::string>    commands;
        std::shared_ptr<WebSocket> socket;
        {
            const std::scoped_lock lock(mutex_);
            commands.swap(commands_);
            socket = socket_;
        }
        for (const std::string& command : commands) {
            if (socket == nullptr) {
                LogWarning("command_dropped", "not connected to GlazeWM: " + command);
                continue;
            }
            try {
                SendSerialized(*socket, command);
            } catch (const std::exception& error) {
                // The receiver sees the same broken connection and reconnects, so this only reports the lost command.
                LogWarning("command_dropped", std::format("{}: {}", command, error.what()));
            }
        }
    }
}

void GlazeWm::SendSerialized(WebSocket& socket, std::string_view text) {
    const std::scoped_lock lock(sendMutex_);
    socket.Send(text);
}

// Posts only when something changed: most focus events move focus within a workspace and leave the list as it was.
void GlazeWm::Publish(std::vector<Workspace> workspaces, WindowManagerStatus status) {
    {
        const std::scoped_lock lock(mutex_);
        if (workspaces == workspaces_ && status == status_) {
            return;
        }
        workspaces_ = std::move(workspaces);
        status_     = std::move(status);
    }
    if (!PostMessageW(target_, changedMessage_, 0, 0)) {
        std::fprintf(stderr, "intarsia: warning component=glazewm event=post_failed error=%lu\n", GetLastError());
    }
}
