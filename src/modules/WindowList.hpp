#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ItemRow.hpp"
#include "modules/WindowIcons.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// One icon per window on the displayed workspace of this bar's monitor, in the window manager's order, each an item in
// the module's item style, item focused for the focused one. The icon is the module's icon size. A click
// focuses the window under it.
class WindowList final : public IModule {
  public:
    WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor, ModuleStyle style);

    bool        Layout(const RenderContext& render) override;
    D2D1_SIZE_F Size() const override;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    void        Click(D2D1_POINT_2F point) override;
    bool        Hover(std::optional<D2D1_POINT_2F> point) override;
    // 0 while an item fades, so the bar draws every display frame until the fades end.
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    // None: the module has no config table, and a left click focuses the window under it.
    const ModuleActions& Actions() const override;

  private:
    HoveredItem _hovered;
    ItemFades   _fades;
    // One shown window. The bitmap is made from pixels on this bar's device, and is null while the icon is unknown.
    struct Slot {
        ManagedWindow                       window;
        std::shared_ptr<const IconPixels>   pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    const ItemStyle& StyleOf(const Slot& slot) const;
    // Layout's work before the fades follow it.
    bool                          LayOutItems(const RenderContext& render);
    std::vector<const ItemStyle*> ItemStyles() const;

    IWindowManager&               _windowManager;
    WindowIcons&                  _icons;
    std::intptr_t                 _monitor;
    ModuleStyle                   _style;
    std::vector<Slot>             _slots;
    ItemRow                       _row     = EmptyItemRow();
    bool                          _laidOut = false;
};
