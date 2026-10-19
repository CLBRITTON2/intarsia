#pragma once

#include "config/Config.hpp"
#include "render/Graphics.hpp"
#include "render/Surface.hpp"
#include "render/WallpaperBlur.hpp"

#include <d2d1_1.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

// Where a context's device pixels fall in DIPs: its pixels per DIP and its world transform's translation.
struct PixelGrid {
    float pixelsPerDip;
    float offsetX;
    float offsetY;
};

PixelGrid PixelGridOf(ID2D1DeviceContext* context);

// Rounds a length to whole device pixels.
float SnapLength(const PixelGrid& grid, float dips);

// Moves each edge to the nearest device pixel boundary.
D2D1_RECT_F SnapRect(const PixelGrid& grid, const D2D1_RECT_F& rect);

// How far a box's content sits in from each of its edges: the padding, then the border on that side.
struct Insets {
    float left;
    float top;
    float right;
    float bottom;
};

Insets ContentInsets(const BoxStyle& style);

bool   SameColor(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second);
bool   SameShadow(const InsetShadow& first, const InsetShadow& second);

// Each drawn side's width rounded down to whole device pixels, never below one. At a fractional scale a 1px line
// otherwise half-lights two rows of pixels and looks blurred. Shadows are snapped as they draw, side by side.
BoxStyle SnapLines(const PixelGrid& grid, const BoxStyle& style);

// The outline of each box for its backdrop to show inside, or none when backdrop has no blur.
std::vector<D2D1_ROUNDED_RECT> BackdropOutlines(const Backdrop& backdrop, const PixelGrid& grid, const std::vector<D2D1_RECT_F>& boxes, const BoxStyle& style);

// Shows backdrop inside each box of style, the system's blur through surface's backdrops or a blur from wallpapers
// drawn on its context, for a window at windowRect. The context is mid-frame with no world transform, and the boxes
// draw over this.
void DrawBackdrops(Surface& surface, const std::vector<WallpaperBlur>& wallpapers, const Backdrop& backdrop, const std::vector<D2D1_RECT_F>& boxes, const BoxStyle& style,
                   const RECT& windowRect);

// The inset shadows and per-side border colors of boxes, drawn once into a bitmap for one device context's device and
// drawn from it again, since each takes dozens of path geometries and layers that D2D tessellates on the CPU at every
// draw, while a fade redraws the same box every frame. Along an axis where the box is longer than twice its corners'
// reach the bitmap holds only both ends and one row between them, stretched to fit, so boxes of one style but different
// lengths share one and a fold that resizes a box draws none.
class BoxCache {
  public:
    explicit BoxCache(ID2D1DeviceContext* context);

    // Paints lines' shadows and border inside box, a snapped outline of radius on the grid's device pixels. Throws
    // DeviceLostError when the device is gone.
    void Draw(ID2D1DeviceContext* context, const PixelGrid& grid, const D2D1_RECT_F& box, float radius, const BoxStyle& lines);

  private:
    struct Entry {
        Border                               border;
        std::vector<InsetShadow>             shadows;
        float                                radius;
        float                                pixelsPerDip;
        std::uint32_t                        width; // pixels
        std::uint32_t                        height;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    };

    // lines' bitmap of width by height pixels, drawn on a miss, and now the most recently drawn.
    ID2D1Bitmap1* BitmapFor(const BoxStyle& lines, float radius, float pixelsPerDip, std::uint32_t width, std::uint32_t height);
    // Draws lines into a new bitmap and stores it, in place of the least recently drawn once the cache is full.
    ID2D1Bitmap1* Render(const BoxStyle& lines, float radius, float pixelsPerDip, std::uint32_t width, std::uint32_t height);

    // Its own, on the caller's device, so a miss draws while the caller's context is mid-frame.
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> _context;
    BrushCache                                 _brushes;
    std::vector<Entry>                         _entries; // least recently drawn first
};

// Fills bounds with fill, then paints style's inset shadows and border inside it, all rounded to style's radius and
// snapped to the context's device pixels. An empty box draws nothing. Throws DeviceLostError when the device is gone.
void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, BoxCache& boxes, const D2D1_RECT_F& bounds, const BoxStyle& style, const D2D1_COLOR_F& fill);

// A window's surface with the brush and box caches made on its context. The caches go before the surface they draw on,
// which member order gives.
struct Canvas {
    Canvas(const GraphicsDevice& graphics, HWND window, std::uint32_t width, std::uint32_t height, std::uint32_t dpi) :
        surface(graphics, window, width, height, dpi), brushes(surface.Context()), boxes(surface.Context()) {}

    Surface    surface;
    BrushCache brushes;
    BoxCache   boxes;
};
