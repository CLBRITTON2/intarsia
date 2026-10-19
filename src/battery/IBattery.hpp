#pragma once

#include <cstdint>
#include <optional>

// Where the battery's charge is going.
enum class ChargeState : std::uint8_t {
    Discharging,
    Charging,
    Plugged // on external power and not charging: full, or held below full by a charge limit
};

// The system's batteries taken together, as Windows reports them.
struct BatteryState {
    ChargeState charge{};
    std::int32_t percent{};                    // 0 to 100
    std::optional<std::int32_t> secondsLeft;   // on battery, once Windows has an estimate
    std::optional<std::int32_t> healthPercent; // full charge capacity against design capacity, when the batteries report both

    bool operator==(const BatteryState&) const = default;
};

// The battery state, sampled as it changes. Reads come from the UI thread and never wait on a battery driver.
class IBattery {
  public:
    virtual ~IBattery() = default;

    // Nullopt on a machine with no battery, or while Windows cannot read it.
    virtual std::optional<BatteryState> State() const = 0;
};
