#include "platform/WebSocket.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"

#include <winsock2.h>

#include <iphlpapi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // Far above any real GlazeWM message (a monitors response is a few KB), low enough that a hostile peer cannot
    // grow the bar's memory without bound.
    constexpr size_t kMaxMessageBytes = size_t{1024} * 1024;

    // Loopback connects, sends and upgrades finish at once, so a short bound only matters when the peer has stopped
    // reading or never answers the upgrade. Once upgraded, receives wait forever (0): an idle IPC socket is normal.
    constexpr std::int32_t kConnectTimeoutMs   = 2000;
    constexpr std::int32_t kSendTimeoutMs      = 2000;
    constexpr std::int32_t kHandshakeTimeoutMs = 2000;
    constexpr DWORD        kIdleTimeoutMs      = 0;
    // Seconds WinHttpWebSocketClose waits for the server's close frame, which bounds how long shutdown can take.
    constexpr DWORD kCloseTimeoutSeconds = 1;

    HINTERNET       Checked(HINTERNET handle, std::string_view call) {
        if (handle == nullptr) {
            ThrowLastError(call);
        }
        return handle;
    }

    UniqueInternet OpenSession() {
        UniqueInternet session(Checked(WinHttpOpen(L"intarsia", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), "WinHttpOpen"));
        // Set on the session so the request and the WebSocket made from it inherit them.
        if (!WinHttpSetTimeouts(session.get(), 0, kConnectTimeoutMs, kSendTimeoutMs, kHandshakeTimeoutMs)) {
            ThrowLastError("WinHttpSetTimeouts");
        }
        return session;
    }

    std::uint16_t LocalPortOf(HINTERNET request) {
        WINHTTP_CONNECTION_INFO info{};
        DWORD                   size = sizeof(info);
        info.cbSize                  = size;
        if (!WinHttpQueryOption(request, WINHTTP_OPTION_CONNECTION_INFO, &info, &size)) {
            ThrowLastError("WinHttpQueryOption(WINHTTP_OPTION_CONNECTION_INFO)");
        }
        if (info.LocalAddress.ss_family != AF_INET) {
            throw std::runtime_error(std::format("the WebSocket's local address has family {}, expected IPv4", static_cast<std::int32_t>(info.LocalAddress.ss_family)));
        }
        sockaddr_in local{};
        std::memcpy(&local, &info.LocalAddress, sizeof(local));
        return ntohs(local.sin_port);
    }

    // MIB_TCPROW_OWNER_PID stores address and port in network byte order, the port in the low 16 bits.
    constexpr std::uint32_t kLoopbackAddress = 0x0100007F; // 127.0.0.1

    std::uint16_t           RowPort(std::uint32_t networkPort) {
        return ntohs(static_cast<u_short>(networkPort));
    }

    std::vector<std::byte> ConnectionTable() {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        // The table can grow between the size query and the read, so retry a few times with the size it reports.
        for (std::int32_t attempt = 0; attempt < 4; ++attempt) {
            std::vector<std::byte> table(size);
            const DWORD            result = GetExtendedTcpTable(table.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
            if (result == NO_ERROR) {
                return table;
            }
            if (result != ERROR_INSUFFICIENT_BUFFER) {
                throw Win32Error("GetExtendedTcpTable(TCP_TABLE_OWNER_PID_CONNECTIONS)", HRESULT_FROM_WIN32(result));
            }
        }
        throw PeerVerificationError("GetExtendedTcpTable kept growing past its reported size");
    }

} // namespace

WebSocket::WebSocket(std::wstring_view host, std::uint16_t port, std::wstring_view path) :
    _session(OpenSession()), _connection(Checked(WinHttpConnect(_session.get(), std::wstring(host).c_str(), port, 0), "WinHttpConnect")) {
    const UniqueInternet request(
        Checked(WinHttpOpenRequest(_connection.get(), L"GET", std::wstring(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0), "WinHttpOpenRequest"));
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET)");
    }
    // The peer is checked only after the upgrade, so until then its answers must not send the bar elsewhere or draw the
    // user's logon out of it.
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_DISABLE_FEATURE)");
    }
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon))) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_AUTOLOGON_POLICY)");
    }
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        ThrowLastError("WinHttpSendRequest");
    }
    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        ThrowLastError("WinHttpReceiveResponse");
    }

    DWORD status     = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        ThrowLastError("WinHttpQueryHeaders(status code)");
    }
    if (status != HTTP_STATUS_SWITCH_PROTOCOLS) {
        throw std::runtime_error(std::format("WebSocket upgrade to port {} answered HTTP {}, expected 101", port, status));
    }
    _localPort = LocalPortOf(request.get());
    // Set on the request, which the WebSocket inherits it from: the WebSocket handle itself rejects the option.
    DWORD idleTimeout = kIdleTimeoutMs;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &idleTimeout, sizeof(idleTimeout))) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_RECEIVE_TIMEOUT)");
    }
    _socket.reset(Checked(WinHttpWebSocketCompleteUpgrade(request.get(), 0), "WinHttpWebSocketCompleteUpgrade"));

    DWORD closeTimeout = kCloseTimeoutSeconds;
    if (!WinHttpSetOption(_socket.get(), WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, &closeTimeout, sizeof(closeTimeout))) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT)");
    }
}

std::uint16_t WebSocket::LocalPort() const {
    return _localPort;
}

void WebSocket::Send(std::string_view text) {
    std::string buffer(text);
    ThrowIfWin32Failed(WinHttpWebSocketSend(_socket.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, buffer.data(), static_cast<DWORD>(buffer.size())), "WinHttpWebSocketSend");
}

std::string WebSocket::Receive() {
    std::string             message;
    std::array<char, 16384> chunk{};
    while (true) {
        DWORD                          read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        ThrowIfWin32Failed(WinHttpWebSocketReceive(_socket.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, &type), "WinHttpWebSocketReceive");
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            throw std::runtime_error("the server closed the WebSocket");
        }
        if (message.size() + read > kMaxMessageBytes) {
            throw std::length_error(std::format("WebSocket message passed {} bytes, the most the bar accepts", kMaxMessageBytes));
        }
        message.append(chunk.data(), read);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            return message;
        }
    }
}

void WebSocket::Close() {
    ThrowIfWin32Failed(WinHttpWebSocketClose(_socket.get(), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0), "WinHttpWebSocketClose");
}

std::uint32_t LoopbackPeerProcess(std::uint16_t clientPort, std::uint16_t serverPort) {
    std::vector<std::byte> table = ConnectionTable();
    MIB_TCPTABLE_OWNER_PID header{};
    std::memcpy(&header, table.data(), offsetof(MIB_TCPTABLE_OWNER_PID, table));

    for (std::uint32_t index = 0; index < header.dwNumEntries; ++index) {
        MIB_TCPROW_OWNER_PID row{};
        std::memcpy(&row, table.data() + offsetof(MIB_TCPTABLE_OWNER_PID, table) + (index * sizeof(MIB_TCPROW_OWNER_PID)), sizeof(row));
        // The server's end: local is the port it serves, remote is the client's port.
        if (row.dwLocalAddr == kLoopbackAddress && row.dwRemoteAddr == kLoopbackAddress && RowPort(row.dwLocalPort) == serverPort && RowPort(row.dwRemotePort) == clientPort) {
            return row.dwOwningPid;
        }
    }
    throw PeerVerificationError(std::format("no connection from 127.0.0.1:{} to 127.0.0.1:{} is open", clientPort, serverPort));
}

void RequireSameUserProcess(std::uint32_t processId, std::wstring_view imageName) {
    const UniqueHandle          process  = OwnHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId), "OpenProcess(listener)");
    const std::filesystem::path image    = ProcessImage(process.get());
    const std::wstring          fileName = image.filename().wstring();
    if (!SameFileName(fileName, imageName)) {
        throw PeerVerificationError(std::format("the listener is {} (pid {}), not {}", ToUtf8(image.wstring()), processId, ToUtf8(imageName)));
    }

    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &rawToken)) {
        throw PeerVerificationError(std::format("cannot read the owner of {} (pid {}): OpenProcessToken failed with {}", ToUtf8(image.wstring()), processId, GetLastError()));
    }
    const UniqueHandle     token(rawToken);
    std::vector<std::byte> peerUser = TokenBuffer(token.get(), TokenUser, "TokenUser");
    std::vector<std::byte> ourUser  = TokenBuffer(GetCurrentProcessToken(), TokenUser, "TokenUser");
    if (!EqualSid(UserSid(peerUser), UserSid(ourUser))) {
        throw PeerVerificationError(std::format("{} (pid {}) runs as a different user", ToUtf8(image.wstring()), processId));
    }
    // A sandboxed process of the same user runs at low integrity (so does every AppContainer), and must not pass.
    const std::uint32_t peerLevel = IntegrityLevel(token.get());
    const std::uint32_t ourLevel  = IntegrityLevel(GetCurrentProcessToken());
    if (peerLevel < ourLevel) {
        throw PeerVerificationError(std::format("{} (pid {}) runs at integrity level 0x{:X}, below the bar's 0x{:X}", ToUtf8(image.wstring()), processId, peerLevel, ourLevel));
    }
}
