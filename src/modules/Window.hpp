#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "modules/Placeholders.hpp"
#include "wm/IWindowManager.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The focused window's title. Empty while nothing has focus.
class Window final : public IModule {
  public:
    // Throws ConfigError when the tooltip names another value or has a spec that cannot render a string.
    Window(WindowConfig config, const IWindowManager& windowManager);

    bool                        Layout(const RenderContext& render) override;
    D2D1_SIZE_F                 Size() const override;
    void                        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    WindowConfig          _config;
    const IWindowManager& _windowManager;
    TextLine              _line;
};

// The values a window tooltip can name: {title}, the whole title.
std::vector<Placeholder> WindowPlaceholders(std::wstring_view title);

// title cut to at most maxLength UTF-16 code units, ending in an ellipsis when cut. Never splits a surrogate pair.
// maxLength is at least 1, as the config requires.
std::wstring TruncateTitle(std::wstring_view title, size_t maxLength);
