#include "platform/WebSocket.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "support/Loopback.hpp"
#include "support/ThrowsWith.hpp"

#include <sddl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

    constexpr wchar_t kTestImage[] = L"intarsia_gtests.exe";

    std::wstring      TestExecutable() {
        std::array<wchar_t, MAX_PATH> path{};
        const DWORD                   length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length == path.size()) {
            ThrowLastError("GetModuleFileNameW");
        }
        return std::wstring(path.data(), length);
    }

    // A primary token like this process's, lowered to low integrity the way a sandbox runs.
    UniqueHandle LowIntegrityToken() {
        HANDLE own = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &own)) {
            ThrowLastError("OpenProcessToken");
        }
        const UniqueHandle ownToken(own);
        HANDLE             duplicate = nullptr;
        if (!DuplicateTokenEx(ownToken.get(), 0, nullptr, SecurityImpersonation, TokenPrimary, &duplicate)) {
            ThrowLastError("DuplicateTokenEx");
        }
        UniqueHandle low(duplicate);

        PSID         sid = nullptr;
        if (!ConvertStringSidToSidW(L"S-1-16-4096", &sid)) {
            ThrowLastError("ConvertStringSidToSidW");
        }
        const UniqueLocal     ownedSid(sid);
        TOKEN_MANDATORY_LABEL label{.Label = {.Sid = sid, .Attributes = SE_GROUP_INTEGRITY}};
        if (!SetTokenInformation(low.get(), TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid(sid))) {
            ThrowLastError("SetTokenInformation(TokenIntegrityLevel)");
        }
        return low;
    }

    std::string UpgradeError(std::uint16_t port) {
        try {
            const WebSocket socket(L"127.0.0.1", port, L"/");
        } catch (const std::runtime_error& error) { return error.what(); }
        return "the upgrade succeeded";
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
        loopback::AnswerUpgrade(peer);
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

// The listener is not yet checked when it answers the upgrade, so its redirect must not send the bar elsewhere.
TEST(WebSocket, UpgradeDoesNotFollowARedirect) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const loopback::Socket  target   = loopback::Listen();
    const std::jthread      server([&listener, &target] {
        const loopback::Socket peer = loopback::Accept(listener);
        loopback::ReceiveHeaders(peer);
        loopback::SendAll(peer, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(target.Port()) + "/\r\nContent-Length: 0\r\n\r\n");
        loopback::DrainUntilClosed(peer);
    });

    EXPECT_NE(UpgradeError(listener.Port()).find("answered HTTP 302"), std::string::npos);
    EXPECT_FALSE(loopback::Readable(target, 0ms));
}

// The listener is not yet checked when it asks for credentials, so the bar must not answer with the user's logon.
TEST(WebSocket, UpgradeDoesNotAnswerAnAuthenticationChallenge) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    bool                    retried  = false;
    std::jthread            server([&listener, &retried] {
        const loopback::Socket peer = loopback::Accept(listener);
        loopback::ReceiveHeaders(peer);
        loopback::SendAll(peer, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Negotiate\r\nWWW-Authenticate: NTLM\r\nContent-Length: 0\r\n\r\n");
        // Readable also on the client's close, so only a byte of a second request counts.
        char byte = 0;
        retried   = (loopback::Readable(peer, 1s) && recv(peer.Get(), &byte, 1, MSG_PEEK) == 1) || loopback::Readable(listener, 0ms);
    });

    EXPECT_NE(UpgradeError(listener.Port()).find("answered HTTP 401"), std::string::npos);
    server.join();
    EXPECT_FALSE(retried);
}

TEST(WebSocket, LocalPortNamesTheConnectionForThePeerCheck) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const std::jthread      server([&listener] {
        const loopback::Socket peer = loopback::Accept(listener);
        loopback::AnswerUpgrade(peer);
        loopback::DrainUntilClosed(peer);
    });

    const WebSocket         socket(L"127.0.0.1", listener.Port(), L"/");
    EXPECT_EQ(LoopbackPeerProcess(socket.LocalPort(), listener.Port()), GetCurrentProcessId());
}

TEST(PeerProcess, FindsTheProcessHoldingTheServerEndOfAConnection) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    const loopback::Socket  client   = loopback::Connect(listener.Port());
    const loopback::Socket  server   = loopback::Accept(listener);
    EXPECT_EQ(LoopbackPeerProcess(client.Port(), listener.Port()), GetCurrentProcessId());
}

TEST(PeerProcess, RejectsAConnectionThatIsNotOpen) {
    const loopback::Winsock winsock;
    const loopback::Socket  listener = loopback::Listen();
    // A listener with nobody connected: there is no server end to find.
    EXPECT_THROW(LoopbackPeerProcess(1, listener.Port()), PeerVerificationError);
}

TEST(PeerProcess, AcceptsAProcessOfThisUserWithTheExpectedImage) {
    EXPECT_NO_THROW(RequireSameUserProcess(GetCurrentProcessId(), kTestImage));
}

TEST(PeerProcess, RejectsAnotherImageName) {
    EXPECT_THROW(RequireSameUserProcess(GetCurrentProcessId(), L"glazewm.exe"), PeerVerificationError);
}

TEST(PeerProcess, RejectsALowIntegrityProcessOfThisUser) {
    const UniqueHandle token      = LowIntegrityToken();
    const std::wstring executable = TestExecutable();
    STARTUPINFOW       startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    // Suspended, so it never runs: only its token and image are read.
    if (!CreateProcessAsUserW(token.get(), executable.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &child)) {
        ThrowLastError("CreateProcessAsUserW");
    }
    const UniqueHandle process(child.hProcess);
    const UniqueHandle thread(child.hThread);

    EXPECT_THAT([&child] { RequireSameUserProcess(child.dwProcessId, kTestImage); }, ThrowsWith<PeerVerificationError>("integrity level"));
    TerminateProcess(process.get(), 0);
}
