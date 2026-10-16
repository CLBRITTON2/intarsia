#include "battery/PowerStatus.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>

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

    // BatteryFromPowerStatus never knows the batteries' health.
    BatteryState WithoutHealth(ChargeState charge, std::int32_t percent, std::optional<std::int32_t> secondsLeft) {
        return BatteryState{
            .charge        = charge,
            .percent       = percent,
            .secondsLeft   = secondsLeft,
            .healthPercent = std::nullopt,
        };
    }

} // namespace

TEST(PowerStatus, ChargingWinsOverPluggedAndOnlyDischargingHasTimeLeft) {
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_OFFLINE, BATTERY_FLAG_LOW, 20, 3900)), WithoutHealth(ChargeState::Discharging, 20, 3900));
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_ONLINE, BATTERY_FLAG_CHARGING | BATTERY_FLAG_HIGH, 70, 5000)), WithoutHealth(ChargeState::Charging, 70, std::nullopt));
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_ONLINE, BATTERY_FLAG_HIGH, 100, BATTERY_LIFE_UNKNOWN)), WithoutHealth(ChargeState::Plugged, 100, std::nullopt));
    // Windows has no estimate yet just after unplugging.
    EXPECT_EQ(BatteryFromPowerStatus(Status(AC_LINE_OFFLINE, BATTERY_FLAG_HIGH, 90, BATTERY_LIFE_UNKNOWN)), WithoutHealth(ChargeState::Discharging, 90, std::nullopt));
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

TEST(PowerStatus, StopsPromptlyRightAfterStarting) {
    ChangeTarget target(kChanged);
    const auto   started = std::chrono::steady_clock::now();
    for (std::int32_t round = 0; round < 10; ++round) {
        const PowerStatus battery(target.signal);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}
