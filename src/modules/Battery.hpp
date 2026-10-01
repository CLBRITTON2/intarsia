#pragma once

#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/IconText.hpp"
#include "modules/IconTextModule.hpp"
#include "modules/Placeholders.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The battery's charge: its format and icon while discharging (the icon by charge), charging or plugged in, drawn in
// the module's .warning colors while discharging at or under warning_at and its .critical colors at or under
// critical_at. Shows nothing on a machine with no battery. Input does nothing unless the config binds actions.
class Battery final : public IconTextModule<BatteryConfig> {
  public:
    // Throws ConfigError when a format uses a name it has no value for.
    Battery(BatteryConfig config, const IBattery& battery);

    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText      Content() override;

    const IBattery& _battery;
};

// The values every battery format can name, from the state.
std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state);

ModuleText               FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state);

// Seconds as hours and minutes, "1:05". Empty while Windows has no estimate.
std::string FormatTimeLeft(std::optional<std::int32_t> seconds);
