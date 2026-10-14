#include "modules/Battery.hpp"

#include "../battery/FakeBattery.hpp"
#include "TextOnlyRender.hpp"
#include "support/ThrowsWith.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

    BatteryConfig TestConfig() {
        return BatteryConfig{
            .formatDischarging = "{capacity}% {time}",
            .formatCharging    = "{capacity}% charging",
            .formatPlugged     = "",
            .tooltip           = "health {health}%",
            .icons             = {L"0", L"1", L"2", L"3", L"4"},
            .iconCharging      = L"C",
            .iconPlugged       = L"P",
            .warningAtPercent  = 40,
            .criticalAtPercent = 15,
            .actions           = kNoActions,
        };
    }

    BatteryState Discharging(std::int32_t percent) {
        return BatteryState{
            .charge        = ChargeState::Discharging,
            .percent       = percent,
            .secondsLeft   = 4500,
            .healthPercent = 91,
        };
    }

} // namespace

TEST(Battery, EachChargeHasItsFormatAndIcon) {
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(64)), NormalText(L"3", L"64% 1:15"));
    BatteryState charging = Discharging(64);
    charging.charge       = ChargeState::Charging;
    EXPECT_EQ(FormatBattery(TestConfig(), charging), NormalText(L"C", L"64% charging"));
    BatteryState plugged = Discharging(100);
    plugged.charge       = ChargeState::Plugged;
    EXPECT_EQ(FormatBattery(TestConfig(), plugged), NormalText(L"P", L""));
    EXPECT_EQ(FormatBattery(TestConfig(), std::nullopt), NormalText(L"", L""));
}

TEST(Battery, WarnsThenTurnsCriticalOnlyWhileDischargingAtOrUnderEachThreshold) {
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(41)).state, TextState::Normal);
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(40)).state, TextState::Warning);
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(16)).state, TextState::Warning);
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(15)).state, TextState::Critical);
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(3)).icon, L"0");
    BatteryState charging = Discharging(5);
    charging.charge       = ChargeState::Charging;
    EXPECT_EQ(FormatBattery(TestConfig(), charging).state, TextState::Normal);
    BatteryState plugged = Discharging(5);
    plugged.charge       = ChargeState::Plugged;
    EXPECT_EQ(FormatBattery(TestConfig(), plugged).state, TextState::Normal);
}

TEST(Battery, TimeLeftIsHoursAndMinutes) {
    EXPECT_EQ(FormatTimeLeft(std::nullopt), "");
    EXPECT_EQ(FormatTimeLeft(59), "0:00");
    EXPECT_EQ(FormatTimeLeft(3900), "1:05");
    EXPECT_EQ(FormatTimeLeft(36000 + 1800), "10:30");
}

TEST(Battery, TooltipCarriesHealthAndIsAbsentWithoutABattery) {
    FakeBattery battery;
    Battery     module(TestConfig(), battery);
    EXPECT_EQ(module.TooltipText(), std::nullopt);
    battery.state = Discharging(50);
    EXPECT_EQ(module.TooltipText(), L"health 91%");
    battery.state->healthPercent = std::nullopt;
    EXPECT_EQ(module.TooltipText(), L"health %");
}

TEST(Battery, LayoutReportsAChangeOnlyWhenTheShownTextChanges) {
    FakeBattery          battery;
    const TextOnlyRender render;
    Battery              module(TestConfig(), battery);
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_EQ(module.Size().width, 0.0f);
    EXPECT_FALSE(module.Layout(render.Context()));
    battery.state = Discharging(64);
    EXPECT_TRUE(module.Layout(render.Context()));
    EXPECT_GT(module.Size().width, 0.0f);
    // Health only reaches the tooltip here, so it changes nothing drawn.
    battery.state->healthPercent = 80;
    EXPECT_FALSE(module.Layout(render.Context()));
}

TEST(Battery, AFormatNamingAnUnknownValueIsAConfigError) {
    FakeBattery   battery;
    BatteryConfig config  = TestConfig();
    config.formatCharging = "{percent}";
    EXPECT_THAT([&] { return Battery(config, battery); }, ThrowsWith<ConfigError>("battery format_charging '{percent}' is not a valid format"));
}
