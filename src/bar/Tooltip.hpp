#pragma once

#include "config/Config.hpp"
#include "platform/Handle.hpp"
#include "render/Box.hpp"
#include "render/Graphics.hpp"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// Where a tooltip goes: beside the hovered module, on the side of the bar that faces the desktop. Screen pixels.
struct TooltipAnchor {
    RECT          target; // the hovered module or item
    RECT          barRect;
    RECT          monitorRect;
    Edge          edge;
    std::uint32_t dpi;
};

// The tooltip's rectangle for a tooltip of size pixels: against the bar's desktop side, centered on the module along
// the bar, and kept on the monitor.
RECT TooltipRect(const TooltipAnchor& anchor, SIZE size);

// A popup showing one module's tooltip text in the bar's font and colors. It never takes focus and stays above other
// windows while shown.
class Tooltip {
  public:
    // wallpapers are the bar's, blurred for its monitor.
    Tooltip(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, const std::vector<WallpaperBlur>& wallpapers);
    Tooltip(const Tooltip&)               = delete;
    Tooltip&    operator=(const Tooltip&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // Throws DeviceLostError when the graphics device is gone.
    void Show(std::wstring_view text, const TooltipAnchor& anchor);
    void Hide();

  private:
    const GraphicsDevice&                _graphics;
    const Config&                        _config;
    IDWriteTextFormat*                   _textFormat;
    const std::vector<WallpaperBlur>&    _wallpapers;
    UniqueReleased<HWND, &DestroyWindow> _window;
    // Only while shown: Show creates it at the text's size and Hide releases it.
    std::unique_ptr<Canvas> _canvas;
};
