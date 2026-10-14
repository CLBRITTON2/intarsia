#pragma once

#include <cstdint>
#include <optional>
#include <string>

// How the machine reaches the internet: through no interface, a Wi-Fi one, or any other. Ethernet stands for every
// link that is not Wi-Fi (a cable, cellular, a VPN's virtual adapter).
enum class NetworkLink : std::uint8_t {
    None,
    Ethernet,
    Wifi
};

// The interface the default route leaves through, as the network module shows it.
struct NetworkState {
    NetworkLink                 link;
    std::wstring                interfaceName; // as network settings name it, such as "Wi-Fi"
    std::wstring                ssid;          // Wi-Fi only, empty when Windows withholds it
    std::optional<std::int32_t> signalPercent; // Wi-Fi only, 0 to 100
    std::wstring                ipv4;          // empty when the interface has none
    double                      receivedBytesPerSecond;
    double                      sentBytesPerSecond;

    // No link, as before the first sample or after a failed one.
    static NetworkState None() {
        return {
            .link                   = NetworkLink::None,
            .interfaceName          = {},
            .ssid                   = {},
            .signalPercent          = std::nullopt,
            .ipv4                   = {},
            .receivedBytesPerSecond = 0.0,
            .sentBytesPerSecond     = 0.0,
        };
    }
};

// The system's network state, sampled as it changes. Reads come from the UI thread and never wait on the network stack.
class INetwork {
  public:
    virtual ~INetwork() = default;

    virtual NetworkState State() const = 0;
};
