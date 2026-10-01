#pragma once

#include <cstdint>
#include <string_view>

enum class ComThreading : std::uint8_t {
    // A single-threaded apartment, which the shell's objects need.
    Single,
    // The multithreaded apartment, so objects calling back on COM's own threads need no marshalling.
    Multi
};

// COM entered on the calling thread, with OLE1 DDE off, for as long as it lives. Every COM object the thread holds is
// released before it goes. name labels the error a failed entry throws, such as one into the other apartment.
class ComScope {
  public:
    ComScope(ComThreading threading, std::string_view name);
    ~ComScope();
    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;
};
