#include "modules/Counter.hpp"

#include "platform/Text.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

Counter::Counter(const CounterConfig& config, const CounterSampler& sampler) : TextModule(config.text, "counter", CounterPlaceholders(0.0)), _sampler(sampler) {}

std::optional<std::wstring> Counter::TooltipText() {
    const CounterReading reading = _sampler.Reading();
    if (reading.outcome == CounterOutcome::Failed) {
        return ToWide(reading.detail);
    }
    return TextModule::TooltipText();
}

ModuleText Counter::Format(const std::string& spec) {
    const CounterReading reading = _sampler.Reading();
    switch (reading.outcome) {
        case CounterOutcome::Pending: return NormalText(Settings().icon, L"");
        case CounterOutcome::Sampled: return NormalText(Settings().icon, ToWide(FormatPlaceholders(spec, CounterPlaceholders(reading.value))));
        case CounterOutcome::Failed:
            return ModuleText{
                .icon       = Settings().icon,
                .text       = L"unavailable",
                .state      = TextState::Alert,
                .classIndex = 0,
            };
    }
    throw std::invalid_argument(std::format("unknown counter outcome {}", static_cast<std::int32_t>(reading.outcome)));
}

std::vector<Placeholder> CounterPlaceholders(double value) {
    return {{.name = "value", .value = value}, {.name = "rate", .value = FormatRate(value)}};
}
