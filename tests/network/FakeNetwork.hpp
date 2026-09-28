#pragma once

#include "network/INetwork.hpp"

// A network state a test sets directly.
class FakeNetwork final : public INetwork {
  public:
    NetworkState state{.link = NetworkLink::None, .interfaceName = {}, .ssid = {}, .signalPercent = std::nullopt, .ipv4 = {}, .receivedBytesPerSecond = 0.0, .sentBytesPerSecond = 0.0};

    NetworkState State() const override {
        return state;
    }
};
