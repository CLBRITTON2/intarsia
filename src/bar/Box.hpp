#pragma once

#include "config/Config.hpp"
#include "render/BrushCache.hpp"

#include <d2d1_1.h>

// Fills box with fill, then strokes style's border inside it, both rounded to style's radius. An empty box draws
// nothing. Throws DeviceLostError when the device is gone.
void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, const BoxStyle& style, const D2D1_COLOR_F& fill);
