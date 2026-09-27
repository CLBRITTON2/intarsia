#pragma once

#include "modules/IModule.hpp"
#include "modules/WindowIcons.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// One icon per window on the displayed workspace of this bar's monitor, the focused one at full strength and the rest
// dimmed, in the window manager's order. A click focuses the window under it.
class WindowList final : public IModule {
  public:
    WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor);

    bool                         Layout(const RenderContext& render) override;
    D2D1_SIZE_F                  Size() const override;
    void                         Draw(const RenderContext& render, D2D1_POINT_2F origin) const override;
    std::optional<std::uint32_t> MillisecondsUntilChange() const override;
    void                         Click(D2D1_POINT_2F point) override;
    // None: the module has no config table, and a left click focuses the window under it.
    const ModuleActions&        Actions() const override;
    std::optional<std::wstring> TooltipText() override;

  private:
    // One shown window. The bitmap is made from pixels on this bar's device, and is null while the icon is unknown.
    struct Slot {
        ManagedWindow                       window;
        std::shared_ptr<const IconPixels>   pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    IWindowManager&   _windowManager;
    WindowIcons&      _icons;
    std::intptr_t     _monitor;
    float             _iconSizeDips = 0.0f; // the font's icon size, from the last layout
    std::vector<Slot> _slots;
    bool              _laidOut = false;
};

// The windows on one monitor, in order.
std::vector<ManagedWindow> WindowsOnMonitor(const std::vector<ManagedWindow>& windows, std::intptr_t monitor);

// Which of count icons, each iconSize wide with gap between them, covers x, or nullopt in a gap or past the last.
std::optional<size_t> IconAt(float x, size_t count, float iconSize, float gap);
