#include "render/Box.hpp"

#include "platform/Errors.hpp"
#include "platform/Threads.hpp"
#include "render/Graphics.hpp"
#include "render/Icon.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace {

    constexpr PixelGrid kFractionalScale{
        .pixelsPerDip = 1.25f,
        .offsetX = 0.0f,
        .offsetY = 0.0f,
    };

    constexpr std::uint32_t kCanvasWidth = 100;
    constexpr float kDrawnWidth = 60.0f;
    constexpr std::uint32_t kDrawnHeight = 30;

    Microsoft::WRL::ComPtr<IWICBitmap> CreateCanvasBitmap(IWICImagingFactory* imaging) {
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        ThrowIfFailed(imaging->CreateBitmap(kCanvasWidth, kDrawnHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap), "IWICImagingFactory::CreateBitmap");
        return bitmap;
    }

    Microsoft::WRL::ComPtr<ID2D1DeviceContext> CreateCanvasContext(IWICBitmap* bitmap) {
        Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
        ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)), "D2D1CreateFactory");
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        ThrowIfFailed(
            factory->CreateWicBitmapRenderTarget(
                bitmap, D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &target),
            "ID2D1Factory::CreateWicBitmapRenderTarget");
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
        ThrowIfFailed(target.As(&context), "ID2D1RenderTarget::QueryInterface(ID2D1DeviceContext)");
        return context;
    }

    class BitmapCanvas {
      public:
        explicit BitmapCanvas(float pixelsPerDip) :
            _com(ComThreading::Multi, "box test"), _imaging(CreateImagingFactory()), _bitmap(CreateCanvasBitmap(_imaging.Get())), _context(CreateCanvasContext(_bitmap.Get())),
            _brushes(_context.Get()), _boxes(_context.Get()), _pixelsPerDip(pixelsPerDip) {
            _context->SetDpi(USER_DEFAULT_SCREEN_DPI * pixelsPerDip, USER_DEFAULT_SCREEN_DPI * pixelsPerDip);
        }

        std::vector<std::uint8_t> Draw(const BoxStyle& style, float widthDips) {
            _context->BeginDraw();
            _context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            DrawBox(_context.Get(), _brushes, _boxes, D2D1::RectF(0.0f, 0.0f, widthDips, static_cast<float>(kDrawnHeight) / _pixelsPerDip), style,
                    D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            ThrowIfFailed(_context->EndDraw(), "ID2D1RenderTarget::EndDraw");
            std::vector<std::uint8_t> pixels(static_cast<size_t>(kCanvasWidth) * kDrawnHeight * 4);
            ThrowIfFailed(_bitmap->CopyPixels(nullptr, kCanvasWidth * 4, static_cast<UINT>(pixels.size()), pixels.data()), "IWICBitmap::CopyPixels");
            return pixels;
        }

      private:
        ComScope _com;
        Microsoft::WRL::ComPtr<IWICImagingFactory> _imaging;
        Microsoft::WRL::ComPtr<IWICBitmap> _bitmap;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> _context;
        BrushCache _brushes;
        BoxCache _boxes;
        float _pixelsPerDip;
    };

    std::vector<std::uint8_t> DrawnPixels(const BoxStyle& style) {
        BitmapCanvas canvas(1.0f);
        return canvas.Draw(style, kDrawnWidth);
    }

    std::uint8_t Red(const std::vector<std::uint8_t>& pixels, size_t x, size_t y) {
        return pixels.at((((y * kCanvasWidth) + x) * 4) + 2);
    }

    std::uint8_t Blue(const std::vector<std::uint8_t>& pixels, size_t x, size_t y) {
        return pixels.at(((y * kCanvasWidth) + x) * 4);
    }

    BorderSide Side(float widthDips) {
        return BorderSide{.widthDips = widthDips, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)};
    }

    Border Uniform(float widthDips) {
        return Border{
            .top = Side(widthDips),
            .right = Side(widthDips),
            .bottom = Side(widthDips),
            .left = Side(widthDips),
        };
    }

    BoxStyle Lined(const Border& border, const std::vector<InsetShadow>& shadows) {
        return BoxStyle{
            .background = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f),
            .border = border,
            .radiusDips = 0.0f,
            .padding = {},
            .shadows = shadows,
        };
    }

    // Its hole is rows 5 to height - 2 and columns 3 to width - 4.
    BoxStyle HardShadowed(const D2D1_COLOR_F& color) {
        return Lined(Uniform(0.0f),
                     {InsetShadow{
                         .xDips = 0.0f,
                         .yDips = 2.0f,
                         .blurDips = 0.0f,
                         .spreadDips = 3.0f,
                         .color = color,
                     }});
    }

} // namespace

TEST(PixelGrid, SnapRectMovesEachEdgeToTheNearestDevicePixel) {
    const D2D1_RECT_F snapped = SnapRect(kFractionalScale, D2D1::RectF(10.3f, 0.5f, 20.0f, 30.9f));
    EXPECT_FLOAT_EQ(snapped.left, 10.4f);
    EXPECT_FLOAT_EQ(snapped.top, 0.8f);
    EXPECT_FLOAT_EQ(snapped.right, 20.0f);
    EXPECT_FLOAT_EQ(snapped.bottom, 31.2f);
}

TEST(PixelGrid, SnapRectCountsTheTransformsTranslation) {
    const PixelGrid scrolled{
        .pixelsPerDip = 1.25f,
        .offsetX = 0.5f,
        .offsetY = 0.0f,
    };
    const D2D1_RECT_F snapped = SnapRect(scrolled, D2D1::RectF(10.3f, 0.0f, 20.0f, 10.0f));
    EXPECT_FLOAT_EQ(snapped.left, 10.7f);
}

TEST(Box, SnapLinesRoundsBorderWidthsDownToWholePixelsButKeepsAtLeastOne) {
    const BoxStyle snapped = SnapLines(kFractionalScale,
                                       Lined(
                                           Border{
                                               .top = Side(1.0f),
                                               .right = Side(2.0f),
                                               .bottom = Side(0.5f),
                                               .left = Side(0.0f),
                                           },
                                           {}));
    EXPECT_FLOAT_EQ(snapped.border.top.widthDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.border.right.widthDips, 1.6f);
    EXPECT_FLOAT_EQ(snapped.border.bottom.widthDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.border.left.widthDips, 0.0f);
}

TEST(Box, SnapLinesKeepsAWidthOfWholePixels) {
    const PixelGrid doubled{
        .pixelsPerDip = 2.0f,
        .offsetX = 0.0f,
        .offsetY = 0.0f,
    };
    const BoxStyle snapped = SnapLines(doubled, Lined(Uniform(1.0f), {}));
    EXPECT_FLOAT_EQ(snapped.border.top.widthDips, 1.0f);
}

TEST(Box, BackdropOutlinesAreTheSnappedBoxesRoundedNoMoreThanHalfTheirShorterSide) {
    BoxStyle style = Lined(Uniform(0.0f), {});
    style.radiusDips = 12.0f;
    const std::vector<D2D1_RECT_F> boxes{D2D1::RectF(10.3f, 0.0f, 60.0f, 16.0f), D2D1::RectF(70.0f, 0.0f, 200.0f, 40.0f)};
    const std::vector<D2D1_ROUNDED_RECT> outlines = BackdropOutlines(
        {
            .source = BackdropSource::System,
            .radiusDips = 0.0f,
            .brightness = 1.0f,
            .saturation = 1.0f,
        },
        kFractionalScale, boxes, style);
    ASSERT_EQ(outlines.size(), 2u);
    EXPECT_FLOAT_EQ(outlines[0].rect.left, 10.4f);
    EXPECT_FLOAT_EQ(outlines[0].radiusX, 8.0f);
    EXPECT_FLOAT_EQ(outlines[1].radiusY, 12.0f);
    EXPECT_EQ(BackdropOutlines(
                  {
                      .source = BackdropSource::Wallpaper,
                      .radiusDips = 8.0f,
                      .brightness = 1.0f,
                      .saturation = 1.0f,
                  },
                  kFractionalScale, boxes, style)
                  .size(),
              2u);
    EXPECT_TRUE(BackdropOutlines(
                    {
                        .source = BackdropSource::None,
                        .radiusDips = 0.0f,
                        .brightness = 1.0f,
                        .saturation = 1.0f,
                    },
                    kFractionalScale, boxes, style)
                    .empty());
}

// CSS splits a two-color corner's curve on the diagonal from the corner, with no gap between.
TEST(Box, ACornerOfTwoColorsSplitsItsCurveOnTheDiagonal) {
    const BorderSide red{.widthDips = 1.0f, .color = D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f)};
    const BorderSide blue{.widthDips = 1.0f, .color = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)};
    BoxStyle style = Lined(
        Border{
            .top = red,
            .right = blue,
            .bottom = blue,
            .left = blue,
        },
        {});
    style.radiusDips = 14.0f;
    const std::vector<std::uint8_t> pixels = DrawnPixels(style);
    // Pixel (6, 2) lies on the ring above the diagonal, (2, 6) its mirror below it.
    EXPECT_GT(Red(pixels, 6, 2), Blue(pixels, 6, 2));
    EXPECT_GT(Blue(pixels, 2, 6), Red(pixels, 2, 6));
    EXPECT_GT(Red(pixels, 30, 0), 200);
}

// At 150% a DIP is a pixel and a half, so half a DIP of offset and half of spread each round to a pixel of their own.
TEST(Box, AHardShadowsSideDrawsAsWideAsABorderOfItsWidth) {
    BitmapCanvas canvas(1.5f);
    const std::vector<std::uint8_t> pixels = canvas.Draw(Lined(
                                                             Border{
                                                                 .top = Side(0.0f),
                                                                 .right = Side(0.0f),
                                                                 .bottom = Side(0.0f),
                                                                 .left = BorderSide{.widthDips = 1.0f, .color = D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f)},
                                                             },
                                                             {InsetShadow{
                                                                 .xDips = 0.5f,
                                                                 .yDips = 0.0f,
                                                                 .blurDips = 0.0f,
                                                                 .spreadDips = 0.5f,
                                                                 .color = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f),
                                                             }}),
                                                         kDrawnWidth);
    EXPECT_GT(Red(pixels, 0, 15), 200);
    EXPECT_GT(Blue(pixels, 1, 15), 200);
    EXPECT_LT(Blue(pixels, 2, 15), 50);
    EXPECT_GT(Blue(pixels, 45, 0), 200);
    EXPECT_LT(Blue(pixels, 45, 1), 50);
    EXPECT_LT(Blue(pixels, 89, 15), 50);
}

TEST(Box, AHardShadowsSideNarrowerThanAPixelStillDrawsOne) {
    BitmapCanvas canvas(1.5f);
    const std::vector<std::uint8_t> pixels = canvas.Draw(Lined(Uniform(0.0f),
                                                               {InsetShadow{
                                                                   .xDips = 0.0f,
                                                                   .yDips = 1.0f,
                                                                   .blurDips = 0.0f,
                                                                   .spreadDips = -0.5f,
                                                                   .color = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f),
                                                               }}),
                                                         kDrawnWidth);
    EXPECT_GT(Blue(pixels, 45, 0), 200);
    EXPECT_LT(Blue(pixels, 45, 1), 50);
    EXPECT_LT(Blue(pixels, 0, 15), 50);
}

TEST(Box, ABlurredShadowsSpreadUnderHalfAPixelDrawsAsNone) {
    BitmapCanvas canvas(1.5f);
    const auto blurred = [](float spreadDips) {
        return Lined(Uniform(0.0f),
                     {InsetShadow{
                         .xDips = 0.0f,
                         .yDips = 0.0f,
                         .blurDips = 4.0f,
                         .spreadDips = spreadDips,
                         .color = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f),
                     }});
    };
    EXPECT_EQ(canvas.Draw(blurred(0.3f), kDrawnWidth), canvas.Draw(blurred(0.0f), kDrawnWidth));
}

// A cache key that ignored the shadow's color would draw the first shadow again: a fade frozen on its first frame.
TEST(Box, OneCacheRedrawsAShadowWhoseColorChanged) {
    BitmapCanvas canvas(1.0f);
    const std::vector<std::uint8_t> red = canvas.Draw(HardShadowed(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f)), kDrawnWidth);
    const std::vector<std::uint8_t> blue = canvas.Draw(HardShadowed(D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)), kDrawnWidth);
    EXPECT_GT(Red(red, 30, 4), 200);
    EXPECT_GT(Blue(blue, 30, 4), 200);
    EXPECT_LT(Red(blue, 30, 4), 50);
}

// A box far longer than its edges reach draws from a cached bitmap of both ends and one row and column between.
TEST(Box, OneCacheDrawsOneStyleAtTwoLengthsWithEachEdgeInPlace) {
    BitmapCanvas canvas(1.0f);
    const BoxStyle style = HardShadowed(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f));
    const std::vector<std::uint8_t> narrow = canvas.Draw(style, 60.0f);
    const std::vector<std::uint8_t> wide = canvas.Draw(style, 100.0f);
    EXPECT_GT(Red(narrow, 30, 4), 200);
    EXPECT_LT(Red(narrow, 30, 5), 50);
    EXPECT_LT(Red(narrow, 30, 28), 50);
    EXPECT_GT(Red(narrow, 30, 29), 200);
    EXPECT_GT(Red(narrow, 2, 15), 200);
    EXPECT_LT(Red(narrow, 3, 15), 50);
    EXPECT_LT(Red(narrow, 56, 15), 50);
    EXPECT_GT(Red(narrow, 57, 15), 200);
    EXPECT_GT(Red(wide, 2, 15), 200);
    EXPECT_LT(Red(wide, 3, 15), 50);
    EXPECT_LT(Red(wide, 96, 15), 50);
    EXPECT_GT(Red(wide, 97, 15), 200);
    EXPECT_GT(Red(wide, 50, 29), 200);
}
