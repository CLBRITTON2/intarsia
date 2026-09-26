#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

    inline Socket ListenOn(USHORT port) {
        Socket            listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        const sockaddr_in address = Address(port);
        if (bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(listener.Get(), 1) != 0) {
            throw std::runtime_error("bind or listen on port " + std::to_string(port) + " failed with " + std::to_string(WSAGetLastError()));
        }
        return listener;
    }

    // Listening on a port the OS picks.
    inline Socket Listen() {
        return ListenOn(0);
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

    inline void ReceiveExactly(const Socket& socket, char* data, size_t size) {
        size_t received = 0;
        while (received < size) {
            const int got = recv(socket.Get(), data + received, static_cast<int>(size - received), 0);
            if (got <= 0) {
                throw std::runtime_error("the other end closed mid-frame");
            }
            received += static_cast<size_t>(got);
        }
    }

    // Whether data waits to be read within wait.
    inline bool Readable(const Socket& socket, std::chrono::milliseconds wait) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket.Get(), &readable);
        const timeval timeout{.tv_sec = static_cast<long>(wait.count() / 1000), .tv_usec = static_cast<long>((wait.count() % 1000) * 1000)};
        return select(0, &readable, nullptr, nullptr, &timeout) > 0;
    }

    // RFC 6455 section 4.2.2: base64 of the SHA-1 of the client's key and this GUID. WinHTTP checks it.
    inline std::string AcceptKey(const std::string& key) {
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

    inline std::string HeaderValue(const std::string& headers, const std::string& name) {
        const size_t start = headers.find(name + ": ");
        if (start == std::string::npos) {
            throw std::runtime_error("the request has no " + name + " header");
        }
        const size_t valueStart = start + name.size() + 2;
        return headers.substr(valueStart, headers.find("\r\n", valueStart) - valueStart);
    }

    // Reads the client's upgrade request and accepts it.
    inline void AnswerUpgrade(const Socket& peer) {
        const std::string key = HeaderValue(ReceiveHeaders(peer), "Sec-WebSocket-Key");
        SendAll(peer, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + AcceptKey(key) + "\r\n\r\n");
    }

    // One final, unmasked frame, as a server sends it.
    inline void SendFrame(const Socket& peer, std::uint8_t opcode, std::string_view payload) {
        std::string frame(1, static_cast<char>(0x80u | opcode));
        if (payload.size() < 126) {
            frame += static_cast<char>(payload.size());
        } else if (payload.size() <= 0xFFFF) {
            frame += static_cast<char>(126);
            frame += static_cast<char>((payload.size() >> 8) & 0xFFu);
            frame += static_cast<char>(payload.size() & 0xFFu);
        } else {
            frame += static_cast<char>(127);
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame += static_cast<char>((static_cast<std::uint64_t>(payload.size()) >> shift) & 0xFFu);
            }
        }
        frame += payload;
        SendAll(peer, frame);
    }

    inline void SendText(const Socket& peer, std::string_view text) {
        SendFrame(peer, 0x1, text);
    }

    // The next text message from the client, answering pings on the way, or nullopt once it closes. Assumes the client
    // never fragments, which WinHTTP does not for the short commands the bar sends.
    inline std::optional<std::string> ReceiveText(const Socket& peer) {
        while (true) {
            std::array<char, 2> header{};
            if (recv(peer.Get(), header.data(), 1, 0) != 1) {
                return std::nullopt;
            }
            ReceiveExactly(peer, header.data() + 1, 1);
            const std::uint8_t opcode = static_cast<std::uint8_t>(header[0]) & 0x0Fu;
            std::uint64_t      length = static_cast<std::uint8_t>(header[1]) & 0x7Fu;
            if (length == 126 || length == 127) {
                std::array<char, 8> extended{};
                const size_t        bytes = length == 126 ? 2 : 8;
                ReceiveExactly(peer, extended.data(), bytes);
                length = 0;
                for (size_t index = 0; index < bytes; ++index) {
                    length = (length << 8) | static_cast<std::uint8_t>(extended[index]);
                }
            }
            std::array<char, 4> mask{};
            ReceiveExactly(peer, mask.data(), mask.size());
            std::string payload(static_cast<size_t>(length), '\0');
            ReceiveExactly(peer, payload.data(), payload.size());
            for (size_t index = 0; index < payload.size(); ++index) {
                payload[index] = static_cast<char>(payload[index] ^ mask[index % 4]);
            }
            // The close handshake: echoing the frame lets the client's close finish instead of timing out.
            if (opcode == 0x8) {
                SendFrame(peer, 0x8, payload);
                return std::nullopt;
            }
            if (opcode == 0x9) {
                SendFrame(peer, 0xA, payload);
                continue;
            }
            if (opcode == 0x1) {
                return payload;
            }
        }
    }

} // namespace loopback
