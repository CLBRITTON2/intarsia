#pragma once

#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/Placeholders.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The battery's charge: its format and icon while discharging (the icon by charge), charging or plugged in, drawn in
// the module's .warning colors while discharging at or under warning_at and its .critical colors at or under
// critical_at. Shows nothing on a machine with no battery. Input does nothing unless the config binds actions.
class Battery final : public IModule {
  public:
    // Throws ConfigError when a format uses a name it has no value for.
    Battery(BatteryConfig config, const IBattery& battery);

    bool                        Layout(const RenderContext& render) override;
    D2D1_SIZE_F                 Size() const override;
    void                        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    BatteryConfig   _config;
    const IBattery& _battery;
    TextLine        _line;
};

// The values every battery format can name, from the state.
std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state);

ModuleText               FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state);

// Seconds as hours and minutes, "1:05". Empty while Windows has no estimate.
std::string FormatTimeLeft(std::optional<std::int32_t> seconds);
