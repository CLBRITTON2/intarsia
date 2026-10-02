#pragma once

#include "network/INetwork.hpp"

// A network state a test sets directly.
class FakeNetwork final : public INetwork {
  public:
    NetworkState state = NetworkState::None();

    NetworkState State() const override {
        return state;
    }
};
