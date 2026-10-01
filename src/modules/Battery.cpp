#include "modules/Battery.hpp"

#include "platform/Text.hpp"

#include <format>
#include <stdexcept>
#include <utility>

Battery::Battery(BatteryConfig config, const IBattery& battery) : IconTextModule(std::move(config)), _battery(battery) {
    const std::vector<Placeholder> samples = BatteryPlaceholders(BatteryState{.charge = ChargeState::Discharging, .percent = 80, .secondsLeft = 3600, .healthPercent = 90});
    RequirePlaceholders("battery format_discharging", Settings().formatDischarging, samples);
    RequirePlaceholders("battery format_charging", Settings().formatCharging, samples);
    RequirePlaceholders("battery format_plugged", Settings().formatPlugged, samples);
    RequireOptionalPlaceholders("battery tooltip", Settings().tooltip, samples);
}

ModuleText Battery::Content() {
    return FormatBattery(Settings(), _battery.State());
}

std::optional<std::wstring> Battery::TooltipText() {
    const std::optional<BatteryState> state = _battery.State();
    if (!Settings().tooltip.has_value() || !state.has_value()) {
        return std::nullopt;
    }
    return ToWide(FormatPlaceholders(*Settings().tooltip, BatteryPlaceholders(*state)));
}

std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state) {
    return {{.name = "capacity", .value = std::int64_t{state.percent}},
            {.name = "time", .value = FormatTimeLeft(state.secondsLeft)},
            {.name = "health", .value = state.healthPercent.has_value() ? std::to_string(*state.healthPercent) : std::string()}};
}

ModuleText FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state) {
    if (!state.has_value()) {
        return ModuleText{.icon = {}, .text = {}, .state = TextState::Normal, .classIndex = 0};
    }
    switch (state->charge) {
        case ChargeState::Discharging:
            return ModuleText{.icon       = LevelIcon(config.icons, state->percent),
                              .text       = ToWide(FormatPlaceholders(config.formatDischarging, BatteryPlaceholders(*state))),
                              .state      = state->percent <= config.criticalAtPercent ? TextState::Critical :
                                       state->percent <= config.warningAtPercent       ? TextState::Warning :
                                                                                         TextState::Normal,
                              .classIndex = 0};
        case ChargeState::Charging:
            return ModuleText{
                .icon = config.iconCharging, .text = ToWide(FormatPlaceholders(config.formatCharging, BatteryPlaceholders(*state))), .state = TextState::Normal, .classIndex = 0};
        case ChargeState::Plugged:
            return ModuleText{
                .icon = config.iconPlugged, .text = ToWide(FormatPlaceholders(config.formatPlugged, BatteryPlaceholders(*state))), .state = TextState::Normal, .classIndex = 0};
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
