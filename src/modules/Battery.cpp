#include "modules/Battery.hpp"

#include "platform/Text.hpp"

#include <format>
#include <stdexcept>
#include <utility>

Battery::Battery(BatteryConfig config, const IBattery& battery) :
    _config(std::move(config)), _battery(battery), _shown{.icon = {}, .text = {}, .alert = false}, _text(EmptyIconText()) {
    const std::vector<Placeholder> samples = BatteryPlaceholders(BatteryState{.charge = ChargeState::Discharging, .percent = 80, .secondsLeft = 3600, .healthPercent = 90});
    RequirePlaceholders("battery format_discharging", _config.formatDischarging, samples);
    RequirePlaceholders("battery format_charging", _config.formatCharging, samples);
    RequirePlaceholders("battery format_plugged", _config.formatPlugged, samples);
    if (_config.tooltip.has_value()) {
        RequirePlaceholders("battery tooltip", *_config.tooltip, samples);
    }
}

bool Battery::Layout(const RenderContext& render) {
    BatteryText shown = FormatBattery(_config, _battery.State());
    if (_laidOut && shown == _shown) {
        return false;
    }
    _text    = shown.text.empty() && shown.icon.empty() ? EmptyIconText() : LayOutIconText(render, shown.icon, shown.text);
    _shown   = std::move(shown);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F Battery::Size() const {
    return _text.size;
}

void Battery::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawIconText(render, origin, _text, _shown.alert ? render.alert : render.foreground);
}

std::optional<std::uint32_t> Battery::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool Battery::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void                       Battery::Scroll(ScrollDirection) {}

void                       Battery::Click(D2D1_POINT_2F) {}

void                       Battery::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> Battery::TooltipItem() const {
    return std::nullopt;
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

BatteryText FormatBattery(const BatteryConfig& config, const std::optional<BatteryState>& state) {
    if (!state.has_value()) {
        return BatteryText{.icon = {}, .text = {}, .alert = false};
    }
    switch (state->charge) {
        case ChargeState::Discharging:
            return BatteryText{.icon  = LevelIcon(config.icons, state->percent),
                               .text  = ToWide(FormatPlaceholders(config.formatDischarging, BatteryPlaceholders(*state))),
                               .alert = state->percent <= config.alertAtPercent};
        case ChargeState::Charging:
            return BatteryText{.icon = config.iconCharging, .text = ToWide(FormatPlaceholders(config.formatCharging, BatteryPlaceholders(*state))), .alert = false};
        case ChargeState::Plugged:
            return BatteryText{.icon = config.iconPlugged, .text = ToWide(FormatPlaceholders(config.formatPlugged, BatteryPlaceholders(*state))), .alert = false};
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
