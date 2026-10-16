#include "platform/Wallpaper.hpp"

#include "support/Rect.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    constexpr RECT Rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
        return RECT{
            .left   = left,
            .top    = top,
            .right  = right,
            .bottom = bottom,
        };
    }

    // The right of two monitors side by side.
    constexpr RECT kMonitor     = Rect(1920, 0, 3840, 1080);
    constexpr RECT kDesktopRect = Rect(0, 0, 3840, 1080);

} // namespace

TEST(Wallpaper, PlaceWallpaperLaysTheImageOnTheMonitorAsEachFitDoes) {
    struct Case {
        std::string_view name;
        WallpaperFit     fit;
        D2D1_SIZE_U      imageSize;
        RECT             expected;
    };
    const std::vector<Case> cases{
        {
            .name      = "center keeps its size in the middle",
            .fit       = WallpaperFit::Center,
            .imageSize = D2D1::SizeU(1000, 500),
            .expected  = Rect(2380, 290, 3380, 790),
        },
        {
            .name      = "center hangs past a smaller monitor on every side",
            .fit       = WallpaperFit::Center,
            .imageSize = D2D1::SizeU(2920, 1580),
            .expected  = Rect(1420, -250, 4340, 1330),
        },
        {
            .name      = "tile's first copy is at the top left",
            .fit       = WallpaperFit::Tile,
            .imageSize = D2D1::SizeU(300, 200),
            .expected  = Rect(1920, 0, 2220, 200),
        },
        {
            .name      = "stretch is the monitor",
            .fit       = WallpaperFit::Stretch,
            .imageSize = D2D1::SizeU(300, 200),
            .expected  = kMonitor,
        },
        {
            .name      = "fit is as high as the monitor in the middle",
            .fit       = WallpaperFit::Fit,
            .imageSize = D2D1::SizeU(1000, 1000),
            .expected  = Rect(2340, 0, 3420, 1080),
        },
        {
            .name      = "fill is as wide as the monitor, cut above and below",
            .fit       = WallpaperFit::Fill,
            .imageSize = D2D1::SizeU(1000, 1000),
            .expected  = Rect(1920, -420, 3840, 1500),
        },
        {
            .name      = "span fills the whole desktop, not the monitor",
            .fit       = WallpaperFit::Span,
            .imageSize = D2D1::SizeU(1000, 500),
            .expected  = Rect(0, -420, 3840, 1500),
        },
    };
    for (const Case& test : cases) {
        SCOPED_TRACE(test.name);
        EXPECT_EQ(PlaceWallpaper(test.fit, test.imageSize, kMonitor, kDesktopRect), test.expected);
    }
}
