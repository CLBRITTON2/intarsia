#pragma once

#include "network/INetwork.hpp"
#include "platform/ChangeSignal.hpp"
#include "platform/StoppableThread.hpp"

#include <windows.h>

#include <mutex>

// The network state through the IP Helper API (the default route's interface, its address and byte counters) and the
// WLAN API (the connected network's name and signal). A worker thread samples once a second, since traffic rates need
// a sample on a clock anyway, and posts a change whenever the link differs (SameLink): rates differ on nearly every
// sample, so a module showing one reads them on a tick of its own.
class IpHelper final : public INetwork {
  public:
    explicit IpHelper(ChangeSignal& changes);
    IpHelper(const IpHelper&)               = delete;
    IpHelper&    operator=(const IpHelper&) = delete;

    NetworkState State() const override;

  private:
    void               Run(HANDLE stopEvent);

    ChangeSignal&      _changes;
    mutable std::mutex _mutex; // guards _state
    NetworkState       _state;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

// Whether two states differ in nothing but their rates.
bool SameLink(const NetworkState& first, const NetworkState& second);

// Bytes per second between two samples of a counter, 0 when the counter went backwards (the interface was reset).
double BytesPerSecond(std::uint64_t before, std::uint64_t after, double seconds);
