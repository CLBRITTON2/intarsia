#include "modules/Battery.hpp"

#include "modules/Placeholders.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    // Empty while the batteries do not report it.
    std::string HealthText(std::optional<std::int32_t> healthPercent) {
        if (!healthPercent.has_value()) {
            return std::string();
        }
        return std::to_string(*healthPercent);
    }

    // The values every battery format can name, from the state.
    std::vector<Placeholder> BatteryPlaceholders(const BatteryState& state) {
        return {{.name = "capacity", .value = std::int64_t{state.percent}},
                {.name = "time", .value = FormatTimeLeft(state.secondsLeft)},
                {.name = "health", .value = HealthText(state.healthPercent)}};
    }

    // The colors a discharging battery draws in at percent: critical at or under critical_at, then warning at or under
    // warning_at.
    TextState DischargingState(const BatteryConfig& config, std::int32_t percent) {
        if (percent <= config.criticalAtPercent) {
            return TextState::Critical;
        }
        if (percent <= config.warningAtPercent) {
            return TextState::Warning;
        }
        return TextState::Normal;
    }

} // namespace

Battery::Battery(BatteryConfig config, const IBattery& battery) : IconTextModule(std::move(config)), _battery(battery) {
    const std::vector<Placeholder> samples = BatteryPlaceholders(BatteryState{
        .charge        = ChargeState::Discharging,
        .percent       = 80,
        .secondsLeft   = 3600,
        .healthPercent = 90,
    });
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
    return FormatPlaceholders(*Settings().tooltip, BatteryPlaceholders(*state));
}

ModuleText FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state) {
    if (!state.has_value()) {
        return NormalText(L"", L"");
    }
    switch (state->charge) {
        case ChargeState::Discharging:
            return ModuleText{
                .icon       = LevelIcon(config.icons, state->percent),
                .text       = FormatPlaceholders(config.formatDischarging, BatteryPlaceholders(*state)),
                .state      = DischargingState(config, state->percent),
                .classIndex = 0,
            };
        case ChargeState::Charging: return NormalText(config.iconCharging, FormatPlaceholders(config.formatCharging, BatteryPlaceholders(*state)));
        case ChargeState::Plugged: return NormalText(config.iconPlugged, FormatPlaceholders(config.formatPlugged, BatteryPlaceholders(*state)));
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
