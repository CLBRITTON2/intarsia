#pragma once

// Before winhttp.h, which declares WINHTTP_CONNECTION_INFO only when the socket address types are defined.
#include "platform/Handle.hpp"

#include <winsock2.h>

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using UniqueInternet = UniqueReleased<HINTERNET, &WinHttpCloseHandle>;

// A blocking WebSocket client over WinHTTP. WinHTTP allows one send while a receive is pending, so one thread may
// receive while another sends, but sends must not overlap each other. Close may come from any thread.
class WebSocket {
  public:
    // Connects and completes the upgrade, throwing Win32Error with the failing WinHTTP call.
    WebSocket(std::wstring_view host, std::uint16_t port, std::wstring_view path);
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    // This end's port, which names the connection in the OS's TCP table.
    std::uint16_t LocalPort() const;

    void Send(std::string_view text);

    // One whole message as text. Throws when the connection fails, is closed, or the server closes it.
    std::string Receive();

    // Sends a close frame, which cancels a Receive pending on another thread (ERROR_WINHTTP_OPERATION_CANCELLED).
    // Fails if a Send is still pending, so callers stop sending first. The handle stays open until destruction.
    void Close();

  private:
    UniqueInternet _session;
    UniqueInternet _connection;
    UniqueInternet _socket;
    std::uint16_t _localPort = 0;
};

// Loopback TCP has no authentication of its own, so a client learns who it reached by asking the OS which process
// owns the other end of its connection.
class PeerVerificationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// The process holding the server end of the loopback connection from 127.0.0.1:clientPort to 127.0.0.1:serverPort.
// Naming the connection rather than the listener means a process that took the port after the connect cannot pass.
std::uint32_t LoopbackPeerProcess(std::uint16_t clientPort, std::uint16_t serverPort);

// Throws PeerVerificationError unless the process runs as the current user, at no lower integrity level, and its
// executable is named imageName.
void RequireSameUserProcess(std::uint32_t processId, std::wstring_view imageName);
