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

// Where a bar put a module's content for the frame it is about to draw.
struct ModulePlacement {
    RECT          window;        // the bar's window, in screen pixels
    D2D1_POINT_2F contentOrigin; // in DIPs from the window's top left corner
    std::uint32_t dpi;
};

// One piece of a bar, such as the clock. The bar calls Layout, then Size, then Place and Draw for each frame it draws.
class IModule {
  public:
    virtual ~IModule() = default;

    // Rebuilds what the module draws from its current data. Returns false when that is what the last call built, so the
    // bar can skip a frame no module changed.
    virtual bool        Layout(const RenderContext& render)                           = 0;
    virtual D2D1_SIZE_F Size() const                                                  = 0;
    virtual void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const = 0;

    // The commands the config binds to this module's inputs. Required, so a module whose config takes actions cannot
    // drop them by leaving this out: one without returns kNoActions.
    virtual const ModuleActions& Actions() const = 0;

    // The defaults below are for a module with no clock, no input and no tooltip of its own.

    // Milliseconds until the content changes on its own, or nullopt when only an outside event changes it.
    virtual std::optional<std::uint32_t> MillisecondsUntilChange() const {
        return std::nullopt;
    }

    virtual void Place(const ModulePlacement&) {}

    // A left click, in DIPs from the module's origin as last drawn. Only called when no on_click command is configured.
    virtual void Click(D2D1_POINT_2F) {}

    // A right click, as Click. Only called when no on_right_click action is configured.
    virtual void RightClick(D2D1_POINT_2F) {}

    // A middle click, as Click. Only called when no on_middle_click action is configured.
    virtual void MiddleClick(D2D1_POINT_2F) {}

    // A wheel notch over the module. Only called when no on_scroll_up or on_scroll_down action is configured for it.
    virtual void Scroll(ScrollDirection) {}

    // Where the cursor is, in DIPs from the module's content origin as last drawn, or nullopt once it leaves the
    // module. Returns whether what the module draws changed, such as the item under the cursor, so the bar redraws only
    // then.
    virtual bool Hover(std::optional<D2D1_POINT_2F>) {
        return false;
    }

    // The text to show while the cursor rests on the module, from its current data, or nullopt for no tooltip.
    virtual std::optional<std::wstring> TooltipText() {
        return std::nullopt;
    }

    // The item TooltipText describes, as its box in DIPs from the module's content origin as last drawn, so the tooltip
    // sits against that item. Nullopt when it describes the whole module.
    virtual std::optional<D2D1_RECT_F> TooltipItem() const {
        return std::nullopt;
    }
};

// Actions() for a module whose config binds none.
inline const ModuleActions kNoActions{.click = std::nullopt, .rightClick = std::nullopt, .middleClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
