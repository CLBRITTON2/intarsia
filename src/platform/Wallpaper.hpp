#pragma once

#include <d2d1.h>
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

// How the desktop lays its wallpaper image on a monitor.
enum class WallpaperFit : std::uint8_t {
    Center,  // at its own size in the middle
    Tile,    // at its own size, repeated from the monitor's top left
    Stretch, // to the monitor's size, out of proportion
    Fit,     // the whole image as large as fits, in the middle
    Fill,    // the whole monitor, the image cropped in the middle
    Span,    // one image filling every monitor's rect at once
};

// What the desktop shows on one monitor.
struct Wallpaper {
    std::filesystem::path image; // empty for a solid color
    WallpaperFit fit;
    D2D1_COLOR_F background; // under and around the image
    RECT desktopRect;        // the union of every monitor's rect, which span fills
};

// The desktop reports a wallpaper intarsia cannot place.
class WallpaperError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// The wallpaper of the monitor at monitorRect, in screen pixels. Throws MonitorGoneError when the desktop lists no
// monitor there, as while monitors change, and WallpaperError for a position it does not know.
Wallpaper ReadWallpaper(const RECT& monitorRect);

// Where the desktop draws an image of imageSize pixels (neither side zero) on the monitor at monitorRect, in screen
// pixels. A tile's is its first copy.
RECT PlaceWallpaper(WallpaperFit fit, D2D1_SIZE_U imageSize, const RECT& monitorRect, const RECT& desktopRect);
