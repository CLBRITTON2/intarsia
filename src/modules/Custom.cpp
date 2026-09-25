#include "modules/Custom.hpp"

#include "platform/Text.hpp"

#include <format>
#include <stdexcept>
#include <string>

Custom::Custom(const CustomConfig& config, const CommandPoller& poller) : TextModule(config.text), _poller(poller) {
    RequireFormats("custom", Settings().format, Settings().tooltip, std::string());
}

std::optional<UINT> Custom::MillisecondsUntilChange() const {
    return std::nullopt;
}

std::optional<std::wstring> Custom::TooltipText() {
    const CommandResult result = _poller.Result();
    if (result.outcome == CommandOutcome::Failed) {
        return ToWide(result.detail);
    }
    if (result.tooltip.has_value()) {
        return ToWide(*result.tooltip);
    }
    return TextModule::TooltipText();
}

ModuleText Custom::Format(const std::string& spec) {
    const CommandResult result = _poller.Result();
    switch (result.outcome) {
        case CommandOutcome::Pending: return ModuleText{.text = {}, .alert = false};
        case CommandOutcome::Succeeded: return ModuleText{.text = ToWide(FormatValue(spec, result.text)), .alert = result.alert};
        case CommandOutcome::Failed: return ModuleText{.text = ToWide(result.text), .alert = true};
    }
    throw std::invalid_argument(std::format("unknown command outcome {}", static_cast<int>(result.outcome)));
}
