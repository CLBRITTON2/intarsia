#pragma once

#include "render/BrushCache.hpp"

#include <d2d1_1.h>
#include <dwrite.h>

// What a module needs to lay out and draw one frame. Non-owning: the bar owns every object here.
struct RenderContext {
    ID2D1DeviceContext* context;
    IDWriteFactory*     dwrite;
    IDWriteTextFormat*  textFormat;
    const wchar_t*      iconFont;
    float               iconSizeDips;
    float               iconGapDips; // between an icon and the text after it
    // The module's own colors: its text and its icon.
    ID2D1SolidColorBrush* foreground;
    ID2D1SolidColorBrush* accent;
    ID2D1SolidColorBrush* alert;
    // Any other color a module draws, such as a workspace state's background.
    BrushCache* brushes;
    // The height of the box a module draws in, across the bar, for a module that fills it such as a divider.
    float boxHeightDips;
};
