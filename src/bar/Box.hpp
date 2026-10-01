#pragma once

#include "config/Config.hpp"
#include "render/BrushCache.hpp"

#include <d2d1_1.h>

// How far a box's content sits in from each of its edges: the padding, then the border on that side.
struct Insets {
    float left;
    float top;
    float right;
    float bottom;
};

Insets ContentInsets(const BoxStyle& style);

// Where a context's device pixels fall in DIPs: its pixels per DIP and its world transform's translation.
struct PixelGrid {
    float pixelsPerDip;
    float offsetX;
    float offsetY;
};

PixelGrid PixelGridOf(ID2D1DeviceContext* context);

// Moves each edge to the nearest device pixel boundary.
D2D1_RECT_F SnapRect(const PixelGrid& grid, const D2D1_RECT_F& rect);

// Each drawn side's width rounded down to whole device pixels, never below one, and each shadow's offsets and spread
// rounded to the nearest. At a fractional scale a 1px line otherwise half-lights two rows of pixels and looks blurred.
BoxStyle SnapLines(const PixelGrid& grid, const BoxStyle& style);

// Fills bounds with fill, then paints style's inset shadows and border inside it, all rounded to style's radius and
// snapped to the context's device pixels. An empty box draws nothing. Throws DeviceLostError when the device is gone.
void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& bounds, const BoxStyle& style, const D2D1_COLOR_F& fill);
