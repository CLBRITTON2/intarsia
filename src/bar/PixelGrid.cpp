#include "bar/PixelGrid.hpp"

#include <windows.h>

#include <cmath>

namespace {

    float SnapCoordinate(float pixelsPerDip, float offset, float dips) {
        return (std::round((dips + offset) * pixelsPerDip) / pixelsPerDip) - offset;
    }

} // namespace

PixelGrid PixelGridOf(ID2D1DeviceContext* context) {
    float dpiX = 0.0f;
    float dpiY = 0.0f;
    context->GetDpi(&dpiX, &dpiY);
    D2D1_MATRIX_3X2_F transform{};
    context->GetTransform(&transform);
    return PixelGrid{
        .pixelsPerDip = dpiX / USER_DEFAULT_SCREEN_DPI,
        .offsetX      = transform._31,
        .offsetY      = transform._32,
    };
}

float SnapLength(const PixelGrid& grid, float dips) {
    return std::round(dips * grid.pixelsPerDip) / grid.pixelsPerDip;
}

D2D1_RECT_F SnapRect(const PixelGrid& grid, const D2D1_RECT_F& rect) {
    return D2D1::RectF(SnapCoordinate(grid.pixelsPerDip, grid.offsetX, rect.left), SnapCoordinate(grid.pixelsPerDip, grid.offsetY, rect.top),
                       SnapCoordinate(grid.pixelsPerDip, grid.offsetX, rect.right), SnapCoordinate(grid.pixelsPerDip, grid.offsetY, rect.bottom));
}
