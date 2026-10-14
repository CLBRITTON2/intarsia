#pragma once

#include <cstdint>
#include <stdexcept>
#include <string_view>

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
