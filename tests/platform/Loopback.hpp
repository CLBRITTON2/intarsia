#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>
#include <string>
#include <utility>

// Test-only TCP on 127.0.0.1, for playing the far end of a connection the code under test makes.
namespace loopback {

    class Winsock {
      public:
        Winsock() {
            WSADATA   data{};
            const int result = WSAStartup(MAKEWORD(2, 2), &data);
            if (result != 0) {
                throw std::runtime_error("WSAStartup failed with " + std::to_string(result));
            }
        }
        ~Winsock() {
            WSACleanup();
        }
        Winsock(const Winsock&)            = delete;
        Winsock& operator=(const Winsock&) = delete;
    };

    class Socket {
      public:
        explicit Socket(SOCKET handle) : _handle(handle) {
            if (_handle == INVALID_SOCKET) {
                throw std::runtime_error("socket call failed with " + std::to_string(WSAGetLastError()));
            }
        }
        ~Socket() {
            if (_handle != INVALID_SOCKET) {
                closesocket(_handle);
            }
        }
        Socket(Socket&& other) noexcept : _handle(std::exchange(other._handle, INVALID_SOCKET)) {}
        Socket(const Socket&)            = delete;
        Socket& operator=(const Socket&) = delete;
        Socket& operator=(Socket&&)      = delete;

        SOCKET  Get() const {
            return _handle;
        }

        USHORT Port() const {
            sockaddr_in address{};
            int         size = sizeof(address);
            if (getsockname(_handle, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
                throw std::runtime_error("getsockname failed with " + std::to_string(WSAGetLastError()));
            }
            return ntohs(address.sin_port);
        }

      private:
        SOCKET _handle;
    };

    inline sockaddr_in Address(USHORT port) {
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return address;
    }

    // Listening on a port the OS picks.
    inline Socket Listen() {
        Socket            listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        const sockaddr_in address = Address(0);
        if (bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener.Get(), 1) != 0) {
            throw std::runtime_error("bind or listen failed with " + std::to_string(WSAGetLastError()));
        }
        return listener;
    }

    inline Socket Connect(USHORT port) {
        Socket            client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        const sockaddr_in address = Address(port);
        if (connect(client.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            throw std::runtime_error("connect failed with " + std::to_string(WSAGetLastError()));
        }
        return client;
    }

    inline Socket Accept(const Socket& listener) {
        return Socket(accept(listener.Get(), nullptr, nullptr));
    }

    inline void SendAll(const Socket& socket, std::string_view bytes) {
        if (send(socket.Get(), bytes.data(), static_cast<int>(bytes.size()), 0) != static_cast<int>(bytes.size())) {
            throw std::runtime_error("send failed with " + std::to_string(WSAGetLastError()));
        }
    }

    // Everything up to and including the blank line that ends an HTTP request's headers.
    inline std::string ReceiveHeaders(const Socket& socket) {
        std::string headers;
        char        byte = 0;
        while (!headers.ends_with("\r\n\r\n")) {
            if (recv(socket.Get(), &byte, 1, 0) != 1) {
                throw std::runtime_error("the client closed before finishing its request headers");
            }
            headers += byte;
        }
        return headers;
    }

    // Blocks until the other end closes, reading and dropping whatever it sends.
    inline void DrainUntilClosed(const Socket& socket) {
        char buffer[256];
        while (recv(socket.Get(), buffer, sizeof(buffer), 0) > 0) {}
    }

} // namespace loopback
