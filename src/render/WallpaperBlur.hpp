#pragma once

#include "config/Style.hpp"
#include "platform/Wallpaper.hpp"
#include "render/Graphics.hpp"

#include <d2d1_1.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <vector>

// One monitor's wallpaper as the desktop draws it, blurred and toned by a wallpaper backdrop once, then drawn inside
// any window's boxes on that monitor as the slice of the desktop beneath them, whatever windows lie between.
class WallpaperBlur {
  public:
    // image is wallpaper's, decoded, and null when it has none. Throws DeviceLostError when the device is gone.
    WallpaperBlur(const GraphicsDevice& graphics, const Wallpaper& wallpaper, IWICBitmapSource* image, const RECT& monitorRect, std::uint32_t dpi, const Backdrop& backdrop);

    const Backdrop& BackdropOf() const;

    // Fills each outline (in the context's DIPs) with the blur beneath it, for a window at windowRect on the monitor.
    // The context is mid-frame with no world transform. Throws DeviceLostError when the device is gone.
    void Fill(ID2D1DeviceContext* context, const RECT& windowRect, const std::vector<D2D1_ROUNDED_RECT>& outlines) const;

  private:
    Backdrop                             _backdrop;
    RECT                                 _monitorRect;
    std::uint32_t                        _downscale; // monitor pixels per pixel of _blurred
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> _blurred;
};

// image decoded to premultiplied BGRA pixels.
Microsoft::WRL::ComPtr<IWICBitmapSource> DecodeWallpaper(IWICImagingFactory& imaging, const std::filesystem::path& image);

// One blur per distinct wallpaper backdrop among style's section, tooltip and menu, none (with no wallpaper read)
// while none takes one. Throws as ReadWallpaper and DecodeWallpaper do, and DeviceLostError when the device is gone.
std::vector<WallpaperBlur> BlurWallpapers(const GraphicsDevice& graphics, const StyleConfig& style, const RECT& monitorRect, std::uint32_t dpi);

// The blur made for backdrop. Throws std::logic_error when blurs holds none, which BlurWallpapers rules out for its own
// style.
const WallpaperBlur& BlurFor(const std::vector<WallpaperBlur>& blurs, const Backdrop& backdrop);
