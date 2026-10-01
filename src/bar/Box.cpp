#include "bar/Box.hpp"

#include <algorithm>

// A border is stroked centered on its outline, so the outline is inset by half the width to keep it inside.
void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, const BoxStyle& style, const D2D1_COLOR_F& fill) {
    // An empty module's box has no length, and its inset border would draw inside out.
    if (box.right <= box.left || box.bottom <= box.top) {
        return;
    }
    const float radius = std::min(style.radiusDips, std::min(box.right - box.left, box.bottom - box.top) / 2.0f);
    if (fill.a > 0.0f) {
        context->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brushes.Get(fill));
    }
    const float width = style.borderWidthDips;
    if (width > 0.0f && style.borderColor.a > 0.0f) {
        const float       inset = width / 2.0f;
        const D2D1_RECT_F edge  = D2D1::RectF(box.left + inset, box.top + inset, box.right - inset, box.bottom - inset);
        const float       round = std::max(radius - inset, 0.0f);
        context->DrawRoundedRectangle(D2D1::RoundedRect(edge, round, round), brushes.Get(style.borderColor), width);
    }
}
