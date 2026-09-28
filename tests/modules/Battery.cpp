#include "modules/Battery.hpp"

#include "../battery/FakeBattery.hpp"
#include "TextOnlyRender.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

    BatteryConfig TestConfig() {
        return BatteryConfig{.formatDischarging = "{capacity}% {time}",
                             .formatCharging    = "{capacity}% charging",
                             .formatPlugged     = "",
                             .tooltip           = "health {health}%",
                             .icons             = {L"0", L"1", L"2", L"3", L"4"},
                             .iconCharging      = L"C",
                             .iconPlugged       = L"P",
                             .alertAtPercent    = 15,
                             .actions = {.click = std::nullopt, .rightClick = std::nullopt, .middleClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt}};
    }

    BatteryState Discharging(std::int32_t percent) {
        return BatteryState{.charge = ChargeState::Discharging, .percent = percent, .secondsLeft = 4500, .healthPercent = 91};
    }

} // namespace

TEST(Battery, EachChargeHasItsFormatAndIcon) {
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(64)), (ModuleText{.icon = L"3", .text = L"64% 1:15", .alert = false}));
    BatteryState charging = Discharging(64);
    charging.charge       = ChargeState::Charging;
    EXPECT_EQ(FormatBattery(TestConfig(), charging), (ModuleText{.icon = L"C", .text = L"64% charging", .alert = false}));
    BatteryState plugged = Discharging(100);
    plugged.charge       = ChargeState::Plugged;
    EXPECT_EQ(FormatBattery(TestConfig(), plugged), (ModuleText{.icon = L"P", .text = L"", .alert = false}));
    EXPECT_EQ(FormatBattery(TestConfig(), std::nullopt), (ModuleText{.icon = L"", .text = L"", .alert = false}));
}

TEST(Battery, AlertsOnlyWhileDischargingAtOrUnderTheThreshold) {
    EXPECT_TRUE(FormatBattery(TestConfig(), Discharging(15)).alert);
    EXPECT_FALSE(FormatBattery(TestConfig(), Discharging(16)).alert);
    EXPECT_EQ(FormatBattery(TestConfig(), Discharging(3)).icon, L"0");
    BatteryState charging = Discharging(5);
    charging.charge       = ChargeState::Charging;
    EXPECT_FALSE(FormatBattery(TestConfig(), charging).alert);
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
    EXPECT_THAT([&] { return Battery(config, battery); }, testing::ThrowsMessage<ConfigError>(testing::HasSubstr("battery format_charging '{percent}' is not a valid format")));
}
