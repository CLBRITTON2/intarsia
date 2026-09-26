#include "platform/WebSocket.hpp"

#include "Loopback.hpp"
#include "platform/Errors.hpp"
#include "platform/PeerProcess.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using loopback::AnswerUpgrade;

TEST(WebSocket, UpgradeFailsSoonWhenThePeerNeverAnswers) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const std::jthread      server([&listener] {
        const loopback::Socket peer = loopback::Accept(listener);
        loopback::ReceiveHeaders(peer);
        loopback::DrainUntilClosed(peer);
    });

    const auto              started = std::chrono::steady_clock::now();
    EXPECT_THROW({ const WebSocket socket(L"127.0.0.1", listener.Port(), L"/"); }, Win32Error);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 10s);
}

// The handshake's receive timeout must not carry over: an IPC socket sits idle between events.
TEST(WebSocket, AnIdleReceiveOutlastsTheHandshakeTimeout) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    std::promise<void>      sendNow;
    const std::jthread      server([&listener, go = sendNow.get_future()] {
        const loopback::Socket peer = loopback::Accept(listener);
        AnswerUpgrade(peer);
        go.wait();
        loopback::SendText(peer, "hi");
        loopback::DrainUntilClosed(peer);
    });

    WebSocket               socket(L"127.0.0.1", listener.Port(), L"/");
    auto                    received = std::async(std::launch::async, [&socket] { return socket.Receive(); });
    EXPECT_EQ(received.wait_for(3s), std::future_status::timeout);
    sendNow.set_value();
    EXPECT_EQ(received.get(), "hi");
}

TEST(WebSocket, LocalPortNamesTheConnectionForThePeerCheck) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const std::jthread      server([&listener] {
        const loopback::Socket peer = loopback::Accept(listener);
        AnswerUpgrade(peer);
        loopback::DrainUntilClosed(peer);
    });

    const WebSocket         socket(L"127.0.0.1", listener.Port(), L"/");
    EXPECT_EQ(LoopbackPeerProcess(socket.LocalPort(), listener.Port()), GetCurrentProcessId());
}
