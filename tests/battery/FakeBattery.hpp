#pragma once

#include "battery/IBattery.hpp"

// A battery state a test sets directly. No battery until it does.
class FakeBattery final : public IBattery {
  public:
    std::optional<BatteryState> state;

    std::optional<BatteryState> State() const override {
        return state;
    }
};
