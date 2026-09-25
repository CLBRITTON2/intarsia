#include "modules/Custom.hpp"

#include "platform/Text.hpp"

#include <format>
#include <stdexcept>
#include <string>

Custom::Custom(const CustomConfig& config, const CommandPoller& poller) : TextModule(config.text), _poller(poller) {
    RequireFormat("custom format", Settings().format, std::string());
}

std::optional<UINT> Custom::MillisecondsUntilChange() const {
    return std::nullopt;
}

ModuleText Custom::Text() {
    const CommandResult result = _poller.Result();
    switch (result.outcome) {
        case CommandOutcome::Pending: return ModuleText{.text = {}, .alert = false};
        case CommandOutcome::Succeeded: return ModuleText{.text = ToWide(FormatValue(Settings().format, result.text)), .alert = false};
        case CommandOutcome::Failed: return ModuleText{.text = ToWide(result.text), .alert = true};
    }
    throw std::invalid_argument(std::format("unknown command outcome {}", static_cast<int>(result.outcome)));
}
