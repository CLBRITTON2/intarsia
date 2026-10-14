#include "modules/Custom.hpp"

#include "modules/Placeholders.hpp"
#include "platform/Text.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    // The values a custom format can name: {text}, what the command printed.
    std::vector<Placeholder> CustomPlaceholders(const std::string& text) {
        return {{.name = "text", .value = text}};
    }

    // A json alert draws in the alert color, a json class in that class's colors.
    TextState SucceededState(const CommandResult& result) {
        if (result.alert) {
            return TextState::Alert;
        }
        if (result.classIndex.has_value()) {
            return TextState::Classed;
        }
        return TextState::Normal;
    }

} // namespace

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
        case CommandOutcome::Pending: return NormalText(Settings().icon, L"");
        case CommandOutcome::Succeeded:
            return ModuleText{
                .icon       = Settings().icon,
                .text       = ToWide(FormatPlaceholders(spec, CustomPlaceholders(result.text))),
                .state      = SucceededState(result),
                .classIndex = result.classIndex.value_or(0),
            };
        case CommandOutcome::Failed:
            return ModuleText{
                .icon       = Settings().icon,
                .text       = ToWide(result.text),
                .state      = TextState::Alert,
                .classIndex = 0,
            };
    }
    throw std::invalid_argument(std::format("unknown command outcome {}", static_cast<std::int32_t>(result.outcome)));
}
