#pragma once

#include "config/Config.hpp"
#include "render/Graphics.hpp"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// The latest present the display showed: its present count (as the swap chain numbers its presents) and the refresh
// count it showed on.
struct ShownPresent {
    std::uint32_t present;
    std::uint32_t refresh;
};

// Holds C++/WinRT types, defined in Surface.cpp only.
struct SurfaceVisuals;

// A window's Direct2D drawing target: a flip-model swap chain with premultiplied alpha, shown through the compositor
// so the window gets per-pixel transparency with no redirection bitmap, over the backdrops ShowBackdrops places. Every
// call throws DeviceLostError when the device is gone.
class Surface {
  public:
    // width and height are pixels and must not be zero.
    Surface(const GraphicsDevice& graphics, HWND window, std::uint32_t width, std::uint32_t height, std::uint32_t dpi);
    ~Surface();
    Surface(const Surface&)            = delete;
    Surface& operator=(const Surface&) = delete;

    // The context to draw each frame on, between its BeginDraw and EndDrawAndPresent.
    ID2D1DeviceContext* Context() const;

    void                Resize(std::uint32_t width, std::uint32_t height, std::uint32_t dpi);
    void                EndDrawAndPresent();
    // The swap chain's number for its latest present, as `LatestShown` numbers them.
    std::uint32_t LastPresentCount() const;
    // Null while the swap chain has no statistics yet, or they restarted (DXGI_ERROR_FRAME_STATISTICS_DISJOINT).
    std::optional<ShownPresent> LatestShown() const;

    // Shows the system's blur of whatever lies behind the window inside each outline (in DIPs, on whole pixels), toned
    // by backdrop's brightness and saturation, under the drawing, in place of the outlines the last call showed. A
    // backdrop of another source shows none, since WallpaperBlur draws it into the content.
    void ShowBackdrops(const std::vector<D2D1_ROUNDED_RECT>& outlines, const Backdrop& backdrop);

  private:
    void                                       BindRenderTarget();

    std::shared_ptr<CompositionDevice>         _composition;
    std::uint32_t                              _dpi;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> _context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>    _swapChain;
    std::unique_ptr<SurfaceVisuals>            _visuals;
};
