#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <d2d1.h>
#include <windows.h>

#include <optional>

// One piece of a bar, such as the clock. The bar calls Layout, then Size, then Draw on every frame.
class IModule {
  public:
    virtual ~IModule() = default;

    // Rebuilds what the module draws from its current data.
    virtual void        Layout(const RenderContext& render)                           = 0;
    virtual D2D1_SIZE_F Size() const                                                  = 0;
    virtual void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const = 0;

    // Milliseconds until the content changes on its own, or nullopt when only an outside event changes it.
    virtual std::optional<UINT> MillisecondsUntilChange() const = 0;

    // A left click, in DIPs from the module's origin as last drawn. Only called when no on_click command is configured.
    virtual void Click(D2D1_POINT_2F point) = 0;

    // The commands the config binds to this module's inputs.
    virtual const ModuleActions& Actions() const = 0;
};
