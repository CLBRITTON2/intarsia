#pragma once

#include "config/Config.hpp"
#include "modules/IconText.hpp"
#include "modules/IconTextModule.hpp"
#include "modules/Placeholders.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A module that shows its icon and one formatted value, redrawn on each interval tick: clock, cpu, memory, custom. A
// subclass supplies only the value, formatted with whichever spec it is given: format on the bar, tooltip on hover.
class TextModule : public IconTextModule<TextModuleConfig> {
  public:
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    std::optional<std::wstring>  TooltipText() override;

  protected:
    // Throws ConfigError when the format or tooltip names a value samples lacks or has a spec that does not fit it.
    // type names the module, such as "cpu".
    TextModule(TextModuleConfig config, std::string_view type, const std::vector<Placeholder>& samples);

    // The module's icon and its current value run through spec.
    virtual ModuleText Format(const std::string& spec) = 0;

  private:
    ModuleText Content() final;
};
