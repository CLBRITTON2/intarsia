#include "modules/Custom.hpp"

#include "platform/Text.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

Custom::Custom(const CustomConfig& config, const CommandPoller& poller) : TextModule(config.text, "custom", CustomPlaceholders(std::string())), _poller(poller) {}

std::optional<std::uint32_t> Custom::MillisecondsUntilChange() const {
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
        case CommandOutcome::Pending: return ModuleText{.icon = Settings().icon, .text = {}, .state = TextState::Normal};
        case CommandOutcome::Succeeded:
            return ModuleText{
                .icon = Settings().icon, .text = ToWide(FormatPlaceholders(spec, CustomPlaceholders(result.text))), .state = result.alert ? TextState::Alert : TextState::Normal};
        case CommandOutcome::Failed: return ModuleText{.icon = Settings().icon, .text = ToWide(result.text), .state = TextState::Alert};
    }
    throw std::invalid_argument(std::format("unknown command outcome {}", static_cast<std::int32_t>(result.outcome)));
}

std::vector<Placeholder> CustomPlaceholders(const std::string& text) {
    return {{.name = "text", .value = text}};
}
