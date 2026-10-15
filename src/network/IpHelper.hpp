#pragma once

#include "network/INetwork.hpp"
#include "platform/Handle.hpp"
#include "platform/Threads.hpp"

#include <windows.h>

#include <memory>
#include <mutex>

// Cancels an IP Helper change notification, waiting out a callback in progress. Defined beside the calls that make
// one, since netioapi.h needs the socket headers ahead of it.
struct CancelMibNotification {
    void operator()(HANDLE notification) const;
};
using UniqueMibNotification = std::unique_ptr<void, CancelMibNotification>;

// The network state through the IP Helper API (the default route's interface, its address and byte counters) and the
// WLAN API (the connected network's name and signal). A worker thread samples whenever Windows reports an interface,
// address, route or Wi-Fi connection change, and posts a change whenever the link differs (SameLink). Rates need a
// sample on a clock, so with samplesRates it also samples once a second, and a module showing one reads them on a tick
// of its own.
class IpHelper final : public INetwork {
  public:
    IpHelper(ChangeSignal& changes, bool samplesRates);
    IpHelper(const IpHelper&)               = delete;
    IpHelper&    operator=(const IpHelper&) = delete;

    NetworkState State() const override;
    bool         SamplesRates() const;

  private:
    void               Run(HANDLE stopEvent);

    ChangeSignal&      _changes;
    const bool         _samplesRates;
    UniqueHandle       _linkEvent; // set by the change notifications
    mutable std::mutex _mutex;     // guards _state
    NetworkState       _state;
    // Canceled before _linkEvent closes, since their callbacks set it.
    UniqueMibNotification _interfaceNotification;
    UniqueMibNotification _addressNotification;
    UniqueMibNotification _routeNotification;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

// Whether two states differ in nothing but their rates.
bool SameLink(const NetworkState& first, const NetworkState& second);

// Bytes per second between two samples of a counter, 0 when the counter went backwards (the interface was reset).
double BytesPerSecond(std::uint64_t before, std::uint64_t after, double seconds);
