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

// What the battery module shows for the state as it is.
struct BatteryText {
    std::wstring icon;
    std::wstring text;
    bool         alert;

    bool         operator==(const BatteryText&) const = default;
};

// The battery's charge: its format and icon while discharging (the icon by charge), charging or plugged in, with the
// text in the module's .alert color while discharging at or under alert_at. Shows nothing on a machine with no
// battery. Input does nothing unless the config binds actions.
class Battery final : public IModule {
  public:
    // Throws ConfigError when a format uses a name it has no value for.
    Battery(BatteryConfig config, const IBattery& battery);

    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    void                         RightClick(D2D1_POINT_2F point) override;
    void                         Scroll(ScrollDirection direction) override;
    bool                         Hover(std::optional<D2D1_POINT_2F> point) override;
    const ModuleActions&         Actions() const override;
    std::optional<std::wstring>  TooltipText() override;
    std::optional<D2D1_RECT_F>   TooltipItem() const override;

  private:
    BatteryConfig   _config;
    const IBattery& _battery;
    BatteryText     _shown; // what _text holds, once laid out
    bool            _laidOut = false;
    IconText        _text;
};

// The values every battery format can name, from the state.
std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state);

BatteryText              FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state);

// Seconds as hours and minutes, "1:05". Empty while Windows has no estimate.
std::string FormatTimeLeft(std::optional<std::int32_t> seconds);
