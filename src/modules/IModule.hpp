#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <d2d1.h>
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

enum class ScrollDirection : std::uint8_t {
    Up,
    Down
};

// One piece of a bar, such as the clock. The bar calls Layout, then Size, then Draw for each frame it draws.
class IModule {
  public:
    virtual ~IModule() = default;

    // Rebuilds what the module draws from its current data. Returns false when that is what the last call built, so the
    // bar can skip a frame no module changed.
    virtual bool        Layout(const RenderContext& render)                           = 0;
    virtual D2D1_SIZE_F Size() const                                                  = 0;
    virtual void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const = 0;

    // Milliseconds until the content changes on its own, or nullopt when only an outside event changes it.
    virtual std::optional<std::uint32_t> MillisecondsUntilChange() const = 0;

    // A left click, in DIPs from the module's origin as last drawn. Only called when no on_click command is configured.
    virtual void Click(D2D1_POINT_2F point) = 0;

    // A wheel notch over the module. Only called when no on_scroll_up or on_scroll_down action is configured for it.
    virtual void Scroll(ScrollDirection direction) = 0;

    // The commands the config binds to this module's inputs.
    virtual const ModuleActions& Actions() const = 0;

    // The text to show while the cursor rests on the module, from its current data, or nullopt for no tooltip.
    virtual std::optional<std::wstring> TooltipText() = 0;
};
