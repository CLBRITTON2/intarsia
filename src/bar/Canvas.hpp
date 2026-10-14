#pragma once

#include "bar/Box.hpp"
#include "render/BrushCache.hpp"
#include "render/Graphics.hpp"
#include "render/Surface.hpp"

#include <windows.h>

#include <cstdint>

// A window's surface with the brush and box caches made on its context. The caches go before the surface they draw on,
// which member order gives.
struct Canvas {
    Canvas(const GraphicsDevice& graphics, HWND window, std::uint32_t width, std::uint32_t height, std::uint32_t dpi) :
        surface(graphics, window, width, height, dpi), brushes(surface.Context()), boxes(surface.Context()) {}

    Surface    surface;
    BrushCache brushes;
    BoxCache   boxes;
};
