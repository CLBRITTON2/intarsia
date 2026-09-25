#pragma once

#include "config/Config.hpp"
#include "render/Graphics.hpp"
#include "render/Surface.hpp"

#include <windows.h>

#include <memory>
#include <string_view>

// Where a tooltip goes: beside the hovered module, on the side of the bar that faces the desktop. Screen pixels.
struct TooltipAnchor {
    RECT module;
    RECT bar;
    RECT monitor;
    Edge edge;
    UINT dpi;
};

// The tooltip's rectangle for a tooltip of size pixels: against the bar's desktop side, centered on the module along
// the bar and kept on the monitor.
RECT TooltipRect(const TooltipAnchor& anchor, SIZE size);

// A popup showing one module's tooltip text in the bar's font and colors. It never takes focus and stays above other
// windows while shown.
class Tooltip {
  public:
    Tooltip(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat);
    ~Tooltip();
    Tooltip(const Tooltip&)               = delete;
    Tooltip&    operator=(const Tooltip&) = delete;

    static void RegisterWindowClass(HINSTANCE instance);

    // Throws DeviceLostError when the graphics device is gone.
    void Show(std::wstring_view text, const TooltipAnchor& anchor);
    void Hide();

  private:
    void                  ReleaseSurface();

    const GraphicsDevice& _graphics;
    const Config&         _config;
    IDWriteTextFormat*    _textFormat;
    HWND                  _window;
    // Only while shown: Show creates it at the text's size and Hide releases it.
    std::unique_ptr<Surface>                     _surface;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _backgroundBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> _foregroundBrush;
};
