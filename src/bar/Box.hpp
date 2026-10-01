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

// Fills box with fill, then paints style's inset shadows and border inside it, all rounded to style's radius. An empty
// box draws nothing. Throws DeviceLostError when the device is gone.
void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, const BoxStyle& style, const D2D1_COLOR_F& fill);
