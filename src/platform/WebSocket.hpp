#pragma once

// Before winhttp.h, which declares WINHTTP_CONNECTION_INFO only when the socket address types are defined.
#include <winsock2.h>

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// A blocking WebSocket client over WinHTTP. WinHTTP allows one send while a receive is pending, so one thread may
// receive while another sends, but sends must not overlap each other. Close may come from any thread.
class WebSocket {
  public:
    // Connects and completes the upgrade, throwing Win32Error with the failing WinHTTP call.
    WebSocket(std::wstring_view host, std::uint16_t port, std::wstring_view path);
    ~WebSocket();
    WebSocket(const WebSocket&)            = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    // This end's port, which names the connection in the OS's TCP table.
    std::uint16_t LocalPort() const;

    void          Send(std::string_view text);

    // One whole message as text. Throws when the connection fails, is closed, or the server closes it.
    std::string Receive();

    // Sends a close frame, which cancels a Receive pending on another thread (ERROR_WINHTTP_OPERATION_CANCELLED).
    // Fails if a Send is still pending, so callers stop sending first. The handle stays open until destruction.
    void Close();

  private:
    struct InternetCloser {
        void operator()(HINTERNET handle) const {
            WinHttpCloseHandle(handle);
        }
    };
    using UniqueInternet = std::unique_ptr<void, InternetCloser>;

    UniqueInternet _session;
    UniqueInternet _connection;
    UniqueInternet _socket;
    std::uint16_t  _localPort = 0;
};
