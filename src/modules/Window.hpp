#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/IconText.hpp"
#include "wm/IWindowManager.hpp"

#include <string>
#include <string_view>

// The focused window's title, like Waybar's hyprland/window. Empty while nothing has focus.
class Window final : public IModule {
  public:
    Window(WindowConfig config, const IWindowManager& windowManager);

    void                 Layout(const RenderContext& render) override;
    D2D1_SIZE_F          Size() const override;
    void                 Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<UINT>  MillisecondsUntilChange() const override;
    void                 Click(D2D1_POINT_2F point) override;
    const ModuleActions& Actions() const override;

  private:
    WindowConfig          _config;
    const IWindowManager& _windowManager;
    IconText              _text;
};

// title cut to at most maxLength UTF-16 code units, ending in an ellipsis when cut. Never splits a surrogate pair.
std::wstring TruncateTitle(std::wstring_view title, size_t maxLength);
