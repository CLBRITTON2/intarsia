#pragma once

#include <d2d1_1.h>
#include <dwrite.h>

// What a module needs to lay out and draw one frame. Non-owning: the bar owns every object here.
struct RenderContext {
    ID2D1DeviceContext*   context;
    IDWriteFactory*       dwrite;
    IDWriteTextFormat*    textFormat;
    ID2D1SolidColorBrush* foreground;
    ID2D1SolidColorBrush* accent;
    ID2D1SolidColorBrush* alert;
    float                 iconSizeDips;
};
