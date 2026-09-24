#pragma once

#include <windows.h>

#include <stdexcept>
#include <string_view>

// Loopback TCP has no authentication of its own, so a client learns who it reached by asking the OS which process
// owns the listening socket.
class PeerVerificationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// The process listening on 127.0.0.1:port (or 0.0.0.0:port when nothing binds loopback itself).
DWORD LoopbackListenerProcess(USHORT port);

// Throws PeerVerificationError unless the process runs as the current user and its executable is named imageName.
void RequireSameUserProcess(DWORD processId, std::wstring_view imageName);
