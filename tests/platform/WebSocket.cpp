#include "platform/WebSocket.hpp"

#include "Loopback.hpp"
#include "platform/Errors.hpp"
#include "platform/PeerProcess.hpp"

#include <bcrypt.h>
#include <wincrypt.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

    using namespace std::chrono_literals;

    // RFC 6455 section 4.2.2: base64 of the SHA-1 of the client's key and this GUID. WinHTTP checks it.
    std::string AcceptKey(const std::string& key) {
        std::string           input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::array<UCHAR, 20> digest{};
        const NTSTATUS        status = BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()), digest.data(),
                                                  static_cast<ULONG>(digest.size()));
        std::array<char, 64>  encoded{};
        DWORD                 length = static_cast<DWORD>(encoded.size());
        if (status != 0 || !CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &length)) {
            throw std::runtime_error("cannot compute Sec-WebSocket-Accept");
        }
        return std::string(encoded.data(), length);
    }

    std::string HeaderValue(const std::string& headers, const std::string& name) {
        const size_t start = headers.find(name + ": ");
        if (start == std::string::npos) {
            throw std::runtime_error("the request has no " + name + " header");
        }
        const size_t valueStart = start + name.size() + 2;
        return headers.substr(valueStart, headers.find("\r\n", valueStart) - valueStart);
    }

    void AnswerUpgrade(const loopback::Socket& peer) {
        const std::string key = HeaderValue(loopback::ReceiveHeaders(peer), "Sec-WebSocket-Key");
        loopback::SendAll(peer, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + AcceptKey(key) + "\r\n\r\n");
    }

} // namespace

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
        // A final unmasked text frame holding "hi".
        loopback::SendAll(peer, std::string("\x81\x02hi", 4));
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
