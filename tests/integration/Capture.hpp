#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace integration {

    // What the screen shows over a window, as 0xRRGGBB per pixel, row by row.
    struct Capture {
        std::int32_t               width;
        std::int32_t               height;
        std::vector<std::uint32_t> pixels;
    };

    RECT ScreenRect(HWND window);
    // Copies the window's rect from the screen, since a DirectComposition window draws nothing into its own DC.
    Capture CaptureScreenOver(HWND window);
    size_t  CountPixels(const Capture& capture, std::uint32_t rgb);
    // Columns from the leftmost pixel in rgb to the rightmost, both counted, or nullopt when there are none.
    std::optional<std::int32_t> ColumnSpan(const Capture& capture, std::uint32_t rgb);
    // The mean column of the pixels in rgb, or nullopt when there are none.
    std::optional<double> MeanColumn(const Capture& capture, std::uint32_t rgb);

} // namespace integration
