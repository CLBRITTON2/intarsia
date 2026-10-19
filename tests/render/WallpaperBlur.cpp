#include "render/WallpaperBlur.hpp"

#include "config/Style.hpp"
#include "platform/Errors.hpp"
#include "render/Graphics.hpp"
#include "render/Icon.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr std::uint32_t kMonitorWidth = 200;
    constexpr std::uint32_t kMonitorHeight = 100;
    constexpr RECT kMonitor{
        .left = 1000,
        .top = 0,
        .right = 1000 + static_cast<LONG>(kMonitorWidth),
        .bottom = static_cast<LONG>(kMonitorHeight),
    };

    Backdrop WallpaperBackdrop(float radiusDips) {
        return Backdrop{
            .source = BackdropSource::Wallpaper,
            .radiusDips = radiusDips,
            .brightness = 1.0f,
            .saturation = 1.0f,
        };
    }

    Wallpaper Stretched(const std::filesystem::path& image, const D2D1_COLOR_F& background) {
        return Wallpaper{
            .image = image,
            .fit = WallpaperFit::Stretch,
            .background = background,
            .desktopRect = kMonitor,
        };
    }

    ComPtr<IWICBitmap> CreateHalves() {
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kMonitorWidth) * kMonitorHeight * 4);
        for (size_t y = 0; y < kMonitorHeight; ++y) {
            for (size_t x = 0; x < kMonitorWidth; ++x) {
                const size_t at = ((y * kMonitorWidth) + x) * 4;
                if (x < kMonitorWidth / 2) {
                    pixels[at + 2] = 255;
                } else {
                    pixels[at] = 255;
                }
                pixels[at + 3] = 255;
            }
        }
        const ComPtr<IWICImagingFactory> imaging = CreateImagingFactory();
        ComPtr<IWICBitmap> bitmap;
        ThrowIfFailed(imaging->CreateBitmapFromMemory(kMonitorWidth, kMonitorHeight, GUID_WICPixelFormat32bppPBGRA, kMonitorWidth * 4, static_cast<UINT>(pixels.size()),
                                                      pixels.data(), &bitmap),
                      "IWICImagingFactory::CreateBitmapFromMemory");
        return bitmap;
    }

    ComPtr<ID2D1Bitmap1> CreateBitmap(ID2D1DeviceContext* context, D2D1_SIZE_U size, D2D1_BITMAP_OPTIONS options) {
        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(options, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfFailed(context->CreateBitmap(size, nullptr, 0, &properties, &bitmap), "ID2D1DeviceContext::CreateBitmap");
        return bitmap;
    }

    class FilledWindow {
      public:
        FilledWindow(const GraphicsDevice& graphics, const WallpaperBlur& blur, const RECT& windowRect, std::uint32_t dpi) :
            _width(static_cast<std::uint32_t>(windowRect.right - windowRect.left)) {
            ComPtr<ID2D1DeviceContext> context;
            ThrowIfFailed(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context), "ID2D1Device::CreateDeviceContext");
            const D2D1_SIZE_U size = D2D1::SizeU(_width, static_cast<std::uint32_t>(windowRect.bottom - windowRect.top));
            const ComPtr<ID2D1Bitmap1> drawn = CreateBitmap(context.Get(), size, D2D1_BITMAP_OPTIONS_TARGET);
            const auto dips = static_cast<float>(dpi);
            context->SetTarget(drawn.Get());
            context->SetDpi(dips, dips);
            const float pixelsPerDip = dips / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
            context->BeginDraw();
            context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            blur.Fill(context.Get(), windowRect,
                      {D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, static_cast<float>(size.width) / pixelsPerDip, static_cast<float>(size.height) / pixelsPerDip), 0.0f, 0.0f)});
            ThrowIfFailed(context->EndDraw(), "ID2D1DeviceContext::EndDraw");

            const ComPtr<ID2D1Bitmap1> readable = CreateBitmap(context.Get(), size, D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
            ThrowIfFailed(readable->CopyFromBitmap(nullptr, drawn.Get(), nullptr), "ID2D1Bitmap::CopyFromBitmap");
            D2D1_MAPPED_RECT mapped{};
            ThrowIfFailed(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped), "ID2D1Bitmap1::Map");
            _pixels.resize(static_cast<size_t>(size.width) * size.height * 4);
            for (size_t row = 0; row < size.height; ++row) {
                std::copy_n(mapped.bits + (row * mapped.pitch), static_cast<size_t>(size.width) * 4, _pixels.begin() + static_cast<std::ptrdiff_t>(row * size.width * 4));
            }
            ThrowIfFailed(readable->Unmap(), "ID2D1Bitmap1::Unmap");
        }

        std::array<std::uint8_t, 4> At(size_t x, size_t y) const {
            const size_t at = ((y * _width) + x) * 4;
            return {_pixels.at(at), _pixels.at(at + 1), _pixels.at(at + 2), _pixels.at(at + 3)};
        }

      private:
        std::uint32_t _width;
        std::vector<std::uint8_t> _pixels;
    };

    constexpr std::array<std::uint8_t, 4> kRed{0, 0, 255, 255};
    constexpr std::array<std::uint8_t, 4> kBlue{255, 0, 0, 255};

} // namespace

TEST(WallpaperBlur, ASolidColorWallpaperFillsWithItsBackground) {
    const CompositionThread compositionThread;
    const GraphicsDevice graphics = CreateGraphicsDevice();
    const WallpaperBlur blur(graphics, Stretched({}, D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f)), nullptr, kMonitor, USER_DEFAULT_SCREEN_DPI, WallpaperBackdrop(4.0f));
    const FilledWindow window(graphics, blur, RECT{.left = 1000, .top = 0, .right = 1200, .bottom = 40}, USER_DEFAULT_SCREEN_DPI);
    EXPECT_EQ(window.At(100, 20), (std::array<std::uint8_t, 4>{0, 255, 0, 255}));
}

TEST(WallpaperBlur, AnUnblurredImageLandsBeneathEachWindowAtItsPlaceOnTheMonitor) {
    const CompositionThread compositionThread;
    const GraphicsDevice graphics = CreateGraphicsDevice();
    const ComPtr<IWICBitmap> halves = CreateHalves();
    const WallpaperBlur blur(graphics, Stretched(L"halves.png", D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f)), halves.Get(), kMonitor, USER_DEFAULT_SCREEN_DPI, WallpaperBackdrop(0.0f));

    const FilledWindow across(graphics, blur, RECT{.left = 1000, .top = 0, .right = 1200, .bottom = 40}, USER_DEFAULT_SCREEN_DPI);
    EXPECT_EQ(across.At(50, 20), kRed);
    EXPECT_EQ(across.At(150, 20), kBlue);
    const FilledWindow right(graphics, blur, RECT{.left = 1080, .top = 60, .right = 1200, .bottom = 100}, 144);
    EXPECT_EQ(right.At(10, 20), kRed);
    EXPECT_EQ(right.At(30, 20), kBlue);
}

TEST(WallpaperBlur, ABlurBlendsTheHalvesOnlyNearWhereTheyMeet) {
    const CompositionThread compositionThread;
    const GraphicsDevice graphics = CreateGraphicsDevice();
    const ComPtr<IWICBitmap> halves = CreateHalves();
    const WallpaperBlur blur(graphics, Stretched(L"halves.png", D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f)), halves.Get(), kMonitor, USER_DEFAULT_SCREEN_DPI, WallpaperBackdrop(8.0f));
    const FilledWindow window(graphics, blur, kMonitor, USER_DEFAULT_SCREEN_DPI);
    const std::array<std::uint8_t, 4> edge = window.At(5, 50);
    EXPECT_GT(edge[2], 240);
    EXPECT_LT(edge[0], 15);
    EXPECT_EQ(edge[3], 255);
    const std::array<std::uint8_t, 4> middle = window.At(100, 50);
    EXPECT_GT(middle[2], 64);
    EXPECT_GT(middle[0], 64);
}

TEST(WallpaperBlur, BlurForFindsTheBlurMadeForTheBackdropAndThrowsWithoutOne) {
    const CompositionThread compositionThread;
    const GraphicsDevice graphics = CreateGraphicsDevice();
    const Wallpaper solid = Stretched({}, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
    const std::vector<WallpaperBlur> blurs{
        WallpaperBlur(graphics, solid, nullptr, kMonitor, USER_DEFAULT_SCREEN_DPI, WallpaperBackdrop(2.0f)),
        WallpaperBlur(graphics, solid, nullptr, kMonitor, USER_DEFAULT_SCREEN_DPI, WallpaperBackdrop(6.0f)),
    };
    EXPECT_FLOAT_EQ(BlurFor(blurs, WallpaperBackdrop(6.0f)).BackdropOf().radiusDips, 6.0f);
    EXPECT_THROW(BlurFor(blurs, WallpaperBackdrop(4.0f)), std::logic_error);
}

// Reading the wallpaper needs the desktop, so a style without a wallpaper backdrop must never ask for it.
TEST(WallpaperBlur, BlurWallpapersReadsNothingWithoutAWallpaperBackdrop) {
    const CompositionThread compositionThread;
    const GraphicsDevice graphics = CreateGraphicsDevice();
    const StyleConfig style = ResolveBarStyle(ParseStyleSheet("bar {\n  font: Some Font\n  size: 15\n  color: #102030\n}\nsection {\n  blur: on\n}\n", "style.conf"));
    EXPECT_TRUE(BlurWallpapers(graphics, style, kMonitor, USER_DEFAULT_SCREEN_DPI).empty());
}
