#pragma once

#include "config/Config.hpp"
#include "modules/CounterSampler.hpp"
#include "modules/Placeholders.hpp"
#include "modules/TextModule.hpp"

#include <optional>
#include <string>
#include <vector>

// The values a counter format can name: {value}, the combined value, and {rate}, it read as bytes per second.
std::vector<Placeholder> CounterPlaceholders(double value);

// A performance counter's combined value: format = "{value:.0f}%". Shows nothing before its first sample, and while the
// counter fails, "unavailable" in the alert color with the reason as its tooltip.
class Counter final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that fits neither a number nor text.
    Counter(const CounterConfig& config, const CounterSampler& sampler);

    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText            Format(const std::string& spec) override;

    const CounterSampler& _sampler;
};
