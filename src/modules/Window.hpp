#pragma once

#include "config/Config.hpp"
#include "modules/IconTextModule.hpp"
#include "modules/TextLine.hpp"
#include "wm/IWindowManager.hpp"

#include <optional>
#include <string>
#include <string_view>

// The focused window's title. Empty while nothing has focus.
class Window final : public IconTextModule<WindowConfig> {
  public:
    // Throws ConfigError when the tooltip names another value or has a spec that cannot render a string.
    Window(WindowConfig config, const IWindowManager& windowManager);

    std::optional<std::wstring> TooltipText() override;

  private:
    ModuleText            Content() override;

    const IWindowManager& _windowManager;
};

// title cut to at most maxLength UTF-16 code units, ending in an ellipsis when cut. Never splits a surrogate pair.
// maxLength is at least 1, as the config requires.
std::wstring TruncateTitle(std::wstring_view title, size_t maxLength);
