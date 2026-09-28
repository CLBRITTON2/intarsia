#include "modules/Battery.hpp"

#include "platform/Text.hpp"

#include <format>
#include <stdexcept>
#include <utility>

Battery::Battery(BatteryConfig config, const IBattery& battery) : _config(std::move(config)), _battery(battery) {
    const std::vector<Placeholder> samples = BatteryPlaceholders(BatteryState{.charge = ChargeState::Discharging, .percent = 80, .secondsLeft = 3600, .healthPercent = 90});
    RequirePlaceholders("battery format_discharging", _config.formatDischarging, samples);
    RequirePlaceholders("battery format_charging", _config.formatCharging, samples);
    RequirePlaceholders("battery format_plugged", _config.formatPlugged, samples);
    RequireOptionalPlaceholders("battery tooltip", _config.tooltip, samples);
}

bool Battery::Layout(const RenderContext& render) {
    return _line.Update(render, FormatBattery(_config, _battery.State()));
}

D2D1_SIZE_F Battery::Size() const {
    return _line.Size();
}

void Battery::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

const ModuleActions& Battery::Actions() const {
    return _config.actions;
}

std::optional<std::wstring> Battery::TooltipText() {
    const std::optional<BatteryState> state = _battery.State();
    if (!_config.tooltip.has_value() || !state.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*_config.tooltip, BatteryPlaceholders(*state)));
}

std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state) {
    return {{.name = "capacity", .value = std::int64_t{state.percent}},
            {.name = "time", .value = FormatTimeLeft(state.secondsLeft)},
            {.name = "health", .value = state.healthPercent.has_value() ? std::to_string(*state.healthPercent) : std::string()}};
}

ModuleText FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state) {
    if (!state.has_value()) {
        return ModuleText{.icon = {}, .text = {}, .state = TextState::Normal};
    }
    switch (state->charge) {
        case ChargeState::Discharging:
            return ModuleText{.icon  = LevelIcon(config.icons, state->percent),
                              .text  = ToWide(FormatPlaceholders(config.formatDischarging, BatteryPlaceholders(*state))),
                              .state = state->percent <= config.criticalAtPercent ? TextState::Critical :
                                  state->percent <= config.warningAtPercent       ? TextState::Warning :
                                                                                    TextState::Normal};
        case ChargeState::Charging:
            return ModuleText{.icon = config.iconCharging, .text = ToWide(FormatPlaceholders(config.formatCharging, BatteryPlaceholders(*state))), .state = TextState::Normal};
        case ChargeState::Plugged:
            return ModuleText{.icon = config.iconPlugged, .text = ToWide(FormatPlaceholders(config.formatPlugged, BatteryPlaceholders(*state))), .state = TextState::Normal};
    }
    throw std::invalid_argument(std::format("unknown battery charge {}", static_cast<std::int32_t>(state->charge)));
}

std::string FormatTimeLeft(std::optional<std::int32_t> seconds) {
    if (!seconds.has_value()) {
        return {};
    }
    const std::int32_t minutes = *seconds / 60;
    return std::format("{}:{:02}", minutes / 60, minutes % 60);
}
