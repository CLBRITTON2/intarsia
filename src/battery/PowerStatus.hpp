#pragma once

#include "battery/IBattery.hpp"
#include "platform/Handle.hpp"
#include "platform/Threads.hpp"

#include <windows.h>

#include <powrprof.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

using UniquePowerNotification = UniqueReleased<HPOWERNOTIFY, &PowerSettingUnregisterNotification>;

// The battery state through GetSystemPowerStatus (charge, power source, time left) and the battery drivers' IOCTLs
// (health). A worker thread samples when Windows reports a change of power source or charge, and every 10 s for the
// time estimate, and posts a change whenever the state differs.
class PowerStatus final : public IBattery {
  public:
    explicit PowerStatus(ChangeSignal& changes);
    PowerStatus(const PowerStatus&)                           = delete;
    PowerStatus&                operator=(const PowerStatus&) = delete;

    std::optional<BatteryState> State() const override;

  private:
    void                        Run(HANDLE stopEvent);

    ChangeSignal&               _changes;
    UniqueHandle                _wakeEvent; // set by the power notifications
    mutable std::mutex          _mutex;     // guards _state
    std::optional<BatteryState> _state;
    // Unregistered before _wakeEvent closes, since their callbacks set it.
    UniquePowerNotification _powerSourceNotification;
    UniquePowerNotification _percentNotification;
    // Declared last so every member above exists before the thread starts.
    StoppableThread _thread;
};

// One battery's capacities in mWh, as its driver reports them.
struct BatteryCapacity {
    std::uint32_t designed;
    std::uint32_t fullCharged;
};

// The state GetSystemPowerStatus describes, without health. Nullopt when it reports no battery or an unknown one.
std::optional<BatteryState> BatteryFromPowerStatus(const SYSTEM_POWER_STATUS& power);

// Full charge capacity against design capacity over every battery, as a whole percent. Nullopt with no capacities.
std::optional<std::int32_t> HealthPercent(const std::vector<BatteryCapacity>& batteries);
