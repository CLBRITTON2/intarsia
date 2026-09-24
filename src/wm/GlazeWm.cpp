#include "wm/GlazeWm.hpp"

#include "platform/PeerProcess.hpp"
#include "platform/Text.hpp"
#include "wm/GlazeWmProtocol.hpp"

#include <algorithm>
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
    constexpr wchar_t                   kName[]          = L"GlazeWM";
    constexpr wchar_t                   kImageName[]     = L"glazewm.exe";

} // namespace

GlazeWm::GlazeWm(HWND target, UINT changedMessage) :
    native_(target, changedMessage), target_(target), changedMessage_(changedMessage), stopEvent_(OwnHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW(stop)")),
    status_{.name = kName, .connection = Connection::Disconnected, .detail = "not connected yet"}, stopping_(false), socket_(nullptr), thread_([this] { Run(); }) {}

GlazeWm::~GlazeWm() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        if (socket_ != nullptr) {
            socket_->Abort();
        }
    }
    SetEvent(stopEvent_.get());
    thread_.join();
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

// Retries forever, as the owner chose: restarting GlazeWM must not take the bar down, and the modules show that it is
// disconnected until it is back. Only the change from up to down is logged, and the wait backs off while it stays down.
void GlazeWm::Run() {
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
                std::fprintf(stderr, "intarsia: warning component=glazewm event=disconnected retry_ms=%lld reason=\"%s\"\n", static_cast<long long>(retry.count()),
                             LogExcerpt(error.what(), kLogExcerptBytes).c_str());
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
    WebSocket socket(L"127.0.0.1", kIpcPort, L"/");
    // GlazeWM's IPC has no authentication, so whoever owns the listening port is checked before anything it says is
    // used: any process of any user could have bound 127.0.0.1:6123 while GlazeWM was down.
    RequireSameUserProcess(LoopbackListenerProcess(kIpcPort), kImageName);

    // Registered for the life of this connection so the destructor can abort it, and cleared before it is destroyed.
    struct Registration {
        GlazeWm& owner;
        Registration(GlazeWm& manager, WebSocket& active) : owner(manager) {
            const std::scoped_lock lock(owner.mutex_);
            if (owner.stopping_) {
                throw std::runtime_error("stopping");
            }
            owner.socket_ = &active;
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
    socket.Send(glazewm::SubscribeCommand());
    if (gate.RequestQuery()) {
        socket.Send(glazewm::kQueryMonitors);
    }
    while (true) {
        const glazewm::ServerMessage message = glazewm::ParseServerMessage(socket.Receive());
        if (!message.success) {
            throw glazewm::ProtocolError(
                std::format("GlazeWM rejected \"{}\": {}", LogExcerpt(message.clientMessage, kLogExcerptBytes), LogExcerpt(message.error, kLogExcerptBytes)));
        }
        if (message.kind == glazewm::MessageKind::Event && glazewm::ChangesWorkspaces(message.eventType)) {
            if (gate.RequestQuery()) {
                socket.Send(glazewm::kQueryMonitors);
            }
        } else if (message.kind == glazewm::MessageKind::Response && message.clientMessage == glazewm::kQueryMonitors) {
            Publish(glazewm::WorkspacesFromMonitors(message.data), WindowManagerStatus{.name = kName, .connection = Connection::Connected, .detail = ""});
            if (gate.Answered()) {
                socket.Send(glazewm::kQueryMonitors);
            }
        }
    }
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
