#pragma once

#include <d2d1_1.h>

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
