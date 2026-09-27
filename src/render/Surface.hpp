#pragma once

#include "render/Graphics.hpp"

#include <windows.h>

// A window's Direct2D drawing target: a flip-model swap chain with premultiplied alpha, shown through DirectComposition
// so the window gets per-pixel transparency with no redirection bitmap. Every call throws DeviceLostError when the
// device is gone.
class Surface {
  public:
    // width and height are pixels and must not be zero.
    Surface(const GraphicsDevice& graphics, HWND window, std::uint32_t width, std::uint32_t height, std::uint32_t dpi);

    // The context to draw each frame on, between its BeginDraw and EndDrawAndPresent.
    ID2D1DeviceContext* Context() const;

    void                Resize(std::uint32_t width, std::uint32_t height, std::uint32_t dpi);
    void                EndDrawAndPresent();

  private:
    void                                        BindRenderTarget(std::uint32_t dpi);

    Microsoft::WRL::ComPtr<ID2D1DeviceContext>  _context;
    Microsoft::WRL::ComPtr<IDCompositionTarget> _compositionTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> _visual;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>     _swapChain;
};
