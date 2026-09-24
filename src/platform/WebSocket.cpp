#include "platform/WebSocket.hpp"

#include "platform/Errors.hpp"

#include <array>
#include <format>
#include <stdexcept>
#include <string>

namespace {

    // Far above any real GlazeWM message (a monitors response is a few KB), low enough that a hostile peer cannot
    // grow the bar's memory without bound.
    constexpr size_t kMaxMessageBytes = size_t{1024} * 1024;

    HINTERNET        Checked(HINTERNET handle, std::string_view call) {
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

} // namespace

WebSocket::WebSocket(std::wstring_view host, INTERNET_PORT port, std::wstring_view path) :
    session_(Checked(WinHttpOpen(L"intarsia", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), "WinHttpOpen")),
    connection_(Checked(WinHttpConnect(session_.get(), std::wstring(host).c_str(), port, 0), "WinHttpConnect")), socket_(nullptr) {
    const UniqueInternet request(
        Checked(WinHttpOpenRequest(connection_.get(), L"GET", std::wstring(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0), "WinHttpOpenRequest"));
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
    socket_ = Checked(WinHttpWebSocketCompleteUpgrade(request.get(), 0), "WinHttpWebSocketCompleteUpgrade");
}

WebSocket::~WebSocket() {
    Abort();
}

void WebSocket::Send(std::string_view text) {
    std::string buffer(text);
    CheckedError(WinHttpWebSocketSend(socket_.load(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, buffer.data(), static_cast<DWORD>(buffer.size())), "WinHttpWebSocketSend");
}

std::string WebSocket::Receive() {
    std::string             message;
    std::array<char, 16384> chunk{};
    while (true) {
        DWORD                          read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        CheckedError(WinHttpWebSocketReceive(socket_.load(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, &type), "WinHttpWebSocketReceive");
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

// Closing the handle is how WinHTTP cancels a pending receive, and exchange makes sure it is closed exactly once.
void WebSocket::Abort() {
    HINTERNET socket = socket_.exchange(nullptr);
    if (socket != nullptr) {
        WinHttpCloseHandle(socket);
    }
}
