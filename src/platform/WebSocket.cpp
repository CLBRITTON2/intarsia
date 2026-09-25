#include "platform/WebSocket.hpp"

#include "platform/Errors.hpp"

#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

namespace {

    // Far above any real GlazeWM message (a monitors response is a few KB), low enough that a hostile peer cannot
    // grow the bar's memory without bound.
    constexpr size_t kMaxMessageBytes = size_t{1024} * 1024;

    // Loopback connects, sends and upgrades finish at once, so a short bound only matters when the peer has stopped
    // reading or never answers the upgrade. Once upgraded, receives wait forever (0): an idle IPC socket is normal.
    constexpr int   kConnectTimeoutMs   = 2000;
    constexpr int   kSendTimeoutMs      = 2000;
    constexpr int   kHandshakeTimeoutMs = 2000;
    constexpr DWORD kIdleTimeoutMs      = 0;
    // Seconds WinHttpWebSocketClose waits for the server's close frame, which bounds how long shutdown can take.
    constexpr DWORD kCloseTimeoutSeconds = 1;

    HINTERNET       Checked(HINTERNET handle, std::string_view call) {
        if (handle == nullptr) {
            ThrowLastError(call);
        }
        return handle;
    }

    void CheckedError(DWORD error, std::string_view call) {
        if (error != NO_ERROR) {
            throw Win32Error(call, HRESULT_FROM_WIN32(error));
        }
    }

    HINTERNET OpenSession() {
        HINTERNET session = Checked(WinHttpOpen(L"intarsia", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), "WinHttpOpen");
        // Set on the session so the request and the WebSocket made from it inherit them.
        if (!WinHttpSetTimeouts(session, 0, kConnectTimeoutMs, kSendTimeoutMs, kHandshakeTimeoutMs)) {
            const DWORD error = GetLastError();
            WinHttpCloseHandle(session);
            throw Win32Error("WinHttpSetTimeouts", HRESULT_FROM_WIN32(error));
        }
        return session;
    }

    USHORT LocalPortOf(HINTERNET request) {
        WINHTTP_CONNECTION_INFO info{};
        DWORD                   size = sizeof(info);
        info.cbSize                  = size;
        if (!WinHttpQueryOption(request, WINHTTP_OPTION_CONNECTION_INFO, &info, &size)) {
            ThrowLastError("WinHttpQueryOption(WINHTTP_OPTION_CONNECTION_INFO)");
        }
        if (info.LocalAddress.ss_family != AF_INET) {
            throw std::runtime_error(std::format("the WebSocket's local address has family {}, expected IPv4", static_cast<int>(info.LocalAddress.ss_family)));
        }
        sockaddr_in local{};
        std::memcpy(&local, &info.LocalAddress, sizeof(local));
        // sin_port is in network byte order.
        return static_cast<USHORT>(((local.sin_port & 0xFFu) << 8) | ((local.sin_port >> 8) & 0xFFu));
    }

} // namespace

WebSocket::WebSocket(std::wstring_view host, INTERNET_PORT port, std::wstring_view path) :
    _session(OpenSession()), _connection(Checked(WinHttpConnect(_session.get(), std::wstring(host).c_str(), port, 0), "WinHttpConnect")) {
    const UniqueInternet request(
        Checked(WinHttpOpenRequest(_connection.get(), L"GET", std::wstring(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0), "WinHttpOpenRequest"));
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        ThrowLastError("WinHttpSetOption(WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET)");
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

USHORT WebSocket::LocalPort() const {
    return _localPort;
}

WebSocket::~WebSocket() = default;

void WebSocket::Send(std::string_view text) {
    std::string buffer(text);
    CheckedError(WinHttpWebSocketSend(_socket.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, buffer.data(), static_cast<DWORD>(buffer.size())), "WinHttpWebSocketSend");
}

std::string WebSocket::Receive() {
    std::string             message;
    std::array<char, 16384> chunk{};
    while (true) {
        DWORD                          read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        CheckedError(WinHttpWebSocketReceive(_socket.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, &type), "WinHttpWebSocketReceive");
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
    CheckedError(WinHttpWebSocketClose(_socket.get(), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0), "WinHttpWebSocketClose");
}
