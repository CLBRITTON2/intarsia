#include "platform/Wallpaper.hpp"

#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Handle.hpp"

#include <shobjidl_core.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

    using UniqueCoString = UniqueReleased<PWSTR, &CoTaskMemFree>;

    WallpaperFit FitOf(DESKTOP_WALLPAPER_POSITION position) {
        switch (position) {
            case DWPOS_CENTER: return WallpaperFit::Center;
            case DWPOS_TILE: return WallpaperFit::Tile;
            case DWPOS_STRETCH: return WallpaperFit::Stretch;
            case DWPOS_FIT: return WallpaperFit::Fit;
            case DWPOS_FILL: return WallpaperFit::Fill;
            case DWPOS_SPAN: return WallpaperFit::Span;
        }
        throw WallpaperError(
            std::format("IDesktopWallpaper::GetPosition returned {}, which is none of center, tile, stretch, fit, fill or span", static_cast<std::int32_t>(position)));
    }

    D2D1_COLOR_F OpaqueColor(COLORREF color) {
        return D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f, static_cast<float>(GetGValue(color)) / 255.0f, static_cast<float>(GetBValue(color)) / 255.0f, 1.0f);
    }

    RECT Union(const RECT& first, const RECT& second) {
        return RECT{
            .left   = std::min(first.left, second.left),
            .top    = std::min(first.top, second.top),
            .right  = std::max(first.right, second.right),
            .bottom = std::max(first.bottom, second.bottom),
        };
    }

    // An image of width by height pixels with its center on areaRect's.
    RECT CenteredIn(const RECT& areaRect, double width, double height) {
        const double left = static_cast<double>(areaRect.left) + ((static_cast<double>(areaRect.right - areaRect.left) - width) / 2.0);
        const double top  = static_cast<double>(areaRect.top) + ((static_cast<double>(areaRect.bottom - areaRect.top) - height) / 2.0);
        return RECT{
            .left   = std::lround(left),
            .top    = std::lround(top),
            .right  = std::lround(left + width),
            .bottom = std::lround(top + height),
        };
    }

    // The scale along each axis that makes the image as wide and as high as areaRect.
    struct Scales {
        double width;
        double height;
    };

    Scales ScalesTo(const RECT& areaRect, D2D1_SIZE_U imageSize) {
        return Scales{
            .width  = static_cast<double>(areaRect.right - areaRect.left) / static_cast<double>(imageSize.width),
            .height = static_cast<double>(areaRect.bottom - areaRect.top) / static_cast<double>(imageSize.height),
        };
    }

    RECT ScaledIn(const RECT& areaRect, D2D1_SIZE_U imageSize, double scale) {
        return CenteredIn(areaRect, static_cast<double>(imageSize.width) * scale, static_cast<double>(imageSize.height) * scale);
    }

    // As large as fits inside areaRect.
    RECT FitIn(const RECT& areaRect, D2D1_SIZE_U imageSize) {
        const Scales scales = ScalesTo(areaRect, imageSize);
        return ScaledIn(areaRect, imageSize, std::min(scales.width, scales.height));
    }

    // As small as covers areaRect.
    RECT FillIn(const RECT& areaRect, D2D1_SIZE_U imageSize) {
        const Scales scales = ScalesTo(areaRect, imageSize);
        return ScaledIn(areaRect, imageSize, std::max(scales.width, scales.height));
    }

} // namespace

Wallpaper ReadWallpaper(const RECT& monitorRect) {
    ComPtr<IDesktopWallpaper> desktop;
    ThrowIfFailed(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop)), "CoCreateInstance(DesktopWallpaper)");
    UINT count = 0;
    ThrowIfFailed(desktop->GetMonitorDevicePathCount(&count), "IDesktopWallpaper::GetMonitorDevicePathCount");

    std::optional<UniqueCoString> matched;
    std::optional<RECT>           desktopRect;
    std::string                   seen;
    for (UINT index = 0; index < count; ++index) {
        PWSTR raw = nullptr;
        ThrowIfFailed(desktop->GetMonitorDevicePathAt(index, &raw), "IDesktopWallpaper::GetMonitorDevicePathAt");
        UniqueCoString path(raw);
        RECT           rect{};
        // The desktop also lists monitors it has seen that are not attached, which have no rect.
        const HRESULT read = desktop->GetMonitorRECT(path.get(), &rect);
        if (FAILED(read) || IsRectEmpty(&rect) != 0) {
            seen += std::format(" none(0x{:08X})", static_cast<std::uint32_t>(read));
            continue;
        }
        seen += " " + RectText(rect);
        if (desktopRect.has_value()) {
            desktopRect = Union(*desktopRect, rect);
        } else {
            desktopRect = rect;
        }
        if (EqualRect(&rect, &monitorRect) != 0) {
            matched = std::move(path);
        }
    }
    if (!matched.has_value() || !desktopRect.has_value()) {
        throw MonitorGoneError(std::format("the desktop lists no wallpaper for the monitor at {}, only for:{}", RectText(monitorRect), seen));
    }

    PWSTR image = nullptr;
    ThrowIfFailed(desktop->GetWallpaper(matched->get(), &image), "IDesktopWallpaper::GetWallpaper");
    const UniqueCoString       imagePath(image);
    DESKTOP_WALLPAPER_POSITION position{};
    ThrowIfFailed(desktop->GetPosition(&position), "IDesktopWallpaper::GetPosition");
    COLORREF background = 0;
    ThrowIfFailed(desktop->GetBackgroundColor(&background), "IDesktopWallpaper::GetBackgroundColor");
    return Wallpaper{
        .image       = std::filesystem::path(imagePath.get()),
        .fit         = FitOf(position),
        .background  = OpaqueColor(background),
        .desktopRect = *desktopRect,
    };
}

RECT PlaceWallpaper(WallpaperFit fit, D2D1_SIZE_U imageSize, const RECT& monitorRect, const RECT& desktopRect) {
    const auto width  = static_cast<double>(imageSize.width);
    const auto height = static_cast<double>(imageSize.height);
    switch (fit) {
        case WallpaperFit::Center: return CenteredIn(monitorRect, width, height);
        case WallpaperFit::Tile:
            return RECT{
                .left   = monitorRect.left,
                .top    = monitorRect.top,
                .right  = monitorRect.left + static_cast<LONG>(imageSize.width),
                .bottom = monitorRect.top + static_cast<LONG>(imageSize.height),
            };
        case WallpaperFit::Stretch: return monitorRect;
        case WallpaperFit::Fit: return FitIn(monitorRect, imageSize);
        case WallpaperFit::Fill: return FillIn(monitorRect, imageSize);
        case WallpaperFit::Span: return FillIn(desktopRect, imageSize);
    }
    throw std::invalid_argument(std::format("unknown wallpaper fit {}", static_cast<std::int32_t>(fit)));
}
