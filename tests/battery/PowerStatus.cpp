#include "battery/PowerStatus.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 12;

    SYSTEM_POWER_STATUS     Status(BYTE acLine, BYTE flag, BYTE percent, DWORD secondsLeft) {
        SYSTEM_POWER_STATUS power{};
        power.ACLineStatus       = acLine;
        power.BatteryFlag        = flag;
        power.BatteryLifePercent = percent;
        power.BatteryLifeTime    = secondsLeft;
        return power;
    }

} // namespace

TEST(PowerStatus, ChargingWinsOverPluggedAndOnlyDischargingHasTimeLeft) {
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_OFFLINE, BATTERY_FLAG_LOW, 20, 3900)),
              (BatteryState{.charge = ChargeState::Discharging, .percent = 20, .secondsLeft = 3900, .healthPercent = std::nullopt}));
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_ONLINE, BATTERY_FLAG_CHARGING | BATTERY_FLAG_HIGH, 70, 5000)),
              (BatteryState{.charge = ChargeState::Charging, .percent = 70, .secondsLeft = std::nullopt, .healthPercent = std::nullopt}));
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_ONLINE, BATTERY_FLAG_HIGH, 100, BATTERY_LIFE_UNKNOWN)),
              (BatteryState{.charge = ChargeState::Plugged, .percent = 100, .secondsLeft = std::nullopt, .healthPercent = std::nullopt}));
    // Windows has no estimate yet just after unplugging.
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_OFFLINE, BATTERY_FLAG_HIGH, 90, BATTERY_LIFE_UNKNOWN)),
              (BatteryState{.charge = ChargeState::Discharging, .percent = 90, .secondsLeft = std::nullopt, .healthPercent = std::nullopt}));
}

TEST(PowerStatus, NoBatteryOrAnUnknownOneIsNoState) {
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_ONLINE, BATTERY_FLAG_NO_BATTERY, BATTERY_PERCENTAGE_UNKNOWN, BATTERY_LIFE_UNKNOWN)), std::nullopt);
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_UNKNOWN, BATTERY_FLAG_UNKNOWN, 50, BATTERY_LIFE_UNKNOWN)), std::nullopt);
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_OFFLINE, BATTERY_FLAG_HIGH, BATTERY_PERCENTAGE_UNKNOWN, BATTERY_LIFE_UNKNOWN)), std::nullopt);
}

TEST(PowerStatus, HealthSumsEveryBatteryAndCanPassFull) {
    EXPECT_EQ(HealthPercent({{.designed = 50000, .fullCharged = 43210}}), 86);
    EXPECT_EQ(HealthPercent({{.designed = 30000, .fullCharged = 30000}, {.designed = 10000, .fullCharged = 6000}}), 90);
    // A new pack often holds a little more than its design capacity.
    EXPECT_EQ(HealthPercent({{.designed = 50000, .fullCharged = 51000}}), 102);
    EXPECT_EQ(HealthPercent({}), std::nullopt);
    EXPECT_EQ(HealthPercent({{.designed = 0, .fullCharged = 100}}), std::nullopt);
}

// Samples the machine's real battery. It asserts only what holds with or without one.
TEST(PowerStatus, SamplesTheBatteryAndStopsPromptly) {
    HWND         target = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ChangeSignal changes(target, kChanged);
    {
        const PowerStatus battery(changes);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const std::optional<BatteryState> state = battery.State();
        if (state.has_value()) {
            EXPECT_GE(state->percent, 0);
            EXPECT_LE(state->percent, 100);
            if (state->healthPercent.has_value()) {
                EXPECT_GT(*state->healthPercent, 0);
            }
        }
    }
    const auto started = std::chrono::steady_clock::now();
    for (std::int32_t round = 0; round < 10; ++round) {
        const PowerStatus battery(changes);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
    DestroyWindow(target);
}
