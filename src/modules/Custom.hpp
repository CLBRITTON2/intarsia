#pragma once

#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "modules/TextModule.hpp"

// What its command printed, formatted from a string: format = "{}". While the command fails it shows the reason in the
// alert color, and nothing before the first run finishes.
class Custom final : public TextModule {
  public:
    // Throws ConfigError when the format cannot render a string.
    Custom(const CustomConfig& config, const CommandPoller& poller);

    // Never on its own: the poller posts when the output changes.
    std::optional<UINT> MillisecondsUntilChange() const override;
    // A failed run's full reason, else a json tooltip, else the tooltip format.
    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText           Format(const std::string& spec) override;

    const CommandPoller& _poller;
};
