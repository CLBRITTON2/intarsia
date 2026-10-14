#pragma once

#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "modules/TextModule.hpp"

#include <cstdint>
#include <optional>
#include <string>

// What its command printed: format = "{text}". While the command fails it shows the reason in the alert color, and
// nothing before the first run finishes.
class Custom final : public TextModule {
  public:
    // Throws ConfigError when a format names another value or has a spec that cannot render a string.
    Custom(const CustomConfig& config, const CommandPoller& poller);

    // Never on its own: the poller posts when the output changes.
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    // A failed run's full reason, else a json tooltip, else the tooltip format.
    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText           Format(const std::string& spec) override;

    const CommandPoller& _poller;
};
