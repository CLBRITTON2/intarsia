#include "bar/Box.hpp"

#include "platform/ComScope.hpp"
#include "platform/Errors.hpp"
#include "render/BrushCache.hpp"
#include "render/Icon.hpp"

#include <gtest/gtest.h>

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace {

    constexpr PixelGrid kFractionalScale{
        .pixelsPerDip = 1.25f,
        .offsetX      = 0.0f,
        .offsetY      = 0.0f,
    };

    constexpr std::uint32_t kDrawnWidth  = 60;
    constexpr std::uint32_t kDrawnHeight = 30;

    // style's box filling a clear 96 DPI bitmap of kDrawnWidth by kDrawnHeight, as premultiplied BGRA rows.
    std::vector<std::uint8_t> DrawnPixels(const BoxStyle& style) {
        const ComScope                                   com(ComThreading::Multi, "box test");
        const Microsoft::WRL::ComPtr<IWICImagingFactory> imaging = CreateImagingFactory();
        Microsoft::WRL::ComPtr<IWICBitmap>               bitmap;
        ThrowIfFailed(imaging->CreateBitmap(kDrawnWidth, kDrawnHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap), "IWICImagingFactory::CreateBitmap");
        Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
        ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)), "D2D1CreateFactory");
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        ThrowIfFailed(factory->CreateWicBitmapRenderTarget(
                          bitmap.Get(),
                          D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &target),
                      "ID2D1Factory::CreateWicBitmapRenderTarget");
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
        ThrowIfFailed(target.As(&context), "ID2D1RenderTarget::QueryInterface(ID2D1DeviceContext)");
        BrushCache brushes(context.Get());
        BoxCache   boxes(context.Get());
        context->BeginDraw();
        context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        DrawBox(context.Get(), brushes, boxes, D2D1::RectF(0.0f, 0.0f, static_cast<float>(kDrawnWidth), static_cast<float>(kDrawnHeight)), style,
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        ThrowIfFailed(context->EndDraw(), "ID2D1RenderTarget::EndDraw");
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kDrawnWidth) * kDrawnHeight * 4);
        ThrowIfFailed(bitmap->CopyPixels(nullptr, kDrawnWidth * 4, static_cast<UINT>(pixels.size()), pixels.data()), "IWICBitmap::CopyPixels");
        return pixels;
    }

    std::uint8_t Red(const std::vector<std::uint8_t>& pixels, size_t x, size_t y) {
        return pixels.at((((y * kDrawnWidth) + x) * 4) + 2);
    }

    std::uint8_t Blue(const std::vector<std::uint8_t>& pixels, size_t x, size_t y) {
        return pixels.at(((y * kDrawnWidth) + x) * 4);
    }

    BorderSide Side(float widthDips) {
        return BorderSide{.widthDips = widthDips, .color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)};
    }

    BoxStyle Lined(const Border& border, const std::vector<InsetShadow>& shadows) {
        return BoxStyle{
            .background = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f),
            .border     = border,
            .radiusDips = 0.0f,
            .padding    = {},
            .shadows    = shadows,
        };
    }

} // namespace

TEST(Box, SnapRectMovesEachEdgeToTheNearestDevicePixel) {
    const D2D1_RECT_F snapped = SnapRect(kFractionalScale, D2D1::RectF(10.3f, 0.5f, 20.0f, 30.9f));
    EXPECT_FLOAT_EQ(snapped.left, 10.4f);
    EXPECT_FLOAT_EQ(snapped.top, 0.8f);
    EXPECT_FLOAT_EQ(snapped.right, 20.0f);
    EXPECT_FLOAT_EQ(snapped.bottom, 31.2f);
}

TEST(Box, SnapRectCountsTheTransformsTranslation) {
    const PixelGrid scrolled{
        .pixelsPerDip = 1.25f,
        .offsetX      = 0.5f,
        .offsetY      = 0.0f,
    };
    const D2D1_RECT_F snapped = SnapRect(scrolled, D2D1::RectF(10.3f, 0.0f, 20.0f, 10.0f));
    // 10.3 DIPs lands on device pixel 13.5 once moved by the translation, which rounds to 14.
    EXPECT_FLOAT_EQ(snapped.left, 10.7f);
}

TEST(Box, SnapLinesRoundsBorderWidthsDownToWholePixelsButKeepsAtLeastOne) {
    const BoxStyle snapped = SnapLines(kFractionalScale,
                                       Lined(
                                           Border{
                                               .top    = Side(1.0f),
                                               .right  = Side(2.0f),
                                               .bottom = Side(0.5f),
                                               .left   = Side(0.0f),
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
        .offsetX      = 0.0f,
        .offsetY      = 0.0f,
    };
    const BoxStyle snapped = SnapLines(doubled,
                                       Lined(
                                           Border{
                                               .top    = Side(1.0f),
                                               .right  = Side(1.0f),
                                               .bottom = Side(1.0f),
                                               .left   = Side(1.0f),
                                           },
                                           {}));
    EXPECT_FLOAT_EQ(snapped.border.top.widthDips, 1.0f);
}

TEST(Box, BackdropOutlinesAreTheSnappedBoxesRoundedNoMoreThanHalfTheirShorterSide) {
    BoxStyle style = Lined(
        Border{
            .top    = Side(0.0f),
            .right  = Side(0.0f),
            .bottom = Side(0.0f),
            .left   = Side(0.0f),
        },
        {});
    style.radiusDips = 12.0f;
    const std::vector<D2D1_RECT_F>       boxes{D2D1::RectF(10.3f, 0.0f, 60.0f, 16.0f), D2D1::RectF(70.0f, 0.0f, 200.0f, 40.0f)};
    const std::vector<D2D1_ROUNDED_RECT> outlines = BackdropOutlines(
        {
            .blur       = true,
            .brightness = 1.0f,
            .saturation = 1.0f,
        },
        kFractionalScale, boxes, style);
    ASSERT_EQ(outlines.size(), 2u);
    EXPECT_FLOAT_EQ(outlines[0].rect.left, 10.4f);
    EXPECT_FLOAT_EQ(outlines[0].radiusX, 8.0f);
    EXPECT_FLOAT_EQ(outlines[1].radiusY, 12.0f);
    EXPECT_TRUE(BackdropOutlines(
                    {
                        .blur       = false,
                        .brightness = 1.0f,
                        .saturation = 1.0f,
                    },
                    kFractionalScale, boxes, style)
                    .empty());
}

// A 1px ring round a 14 radius corner, red on top and blue on the left: CSS splits the curve where it meets the
// diagonal from the corner, so the curve is the top's color down to it and the left's from there, with no gap between.
TEST(Box, ACornerOfTwoColorsSplitsItsCurveOnTheDiagonal) {
    const BorderSide red{.widthDips = 1.0f, .color = D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f)};
    const BorderSide blue{.widthDips = 1.0f, .color = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)};
    BoxStyle         style = Lined(
        Border{
                    .top    = red,
                    .right  = blue,
                    .bottom = blue,
                    .left   = blue,
        },
        {});
    style.radiusDips                       = 14.0f;
    const std::vector<std::uint8_t> pixels = DrawnPixels(style);
    // Pixel (6, 2) lies on the ring above the diagonal, (2, 6) its mirror below it.
    EXPECT_GT(Red(pixels, 6, 2), Blue(pixels, 6, 2));
    EXPECT_GT(Blue(pixels, 2, 6), Red(pixels, 2, 6));
    EXPECT_GT(Red(pixels, 30, 0), 200);
}

TEST(Box, SnapLinesRoundsShadowOffsetsAndSpreadButNotBlur) {
    const InsetShadow shadow{
        .xDips      = 0.0f,
        .yDips      = 1.0f,
        .blurDips   = 14.0f,
        .spreadDips = -6.0f,
        .color      = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f),
    };
    const BoxStyle snapped = SnapLines(kFractionalScale,
                                       Lined(
                                           Border{
                                               .top    = Side(0.0f),
                                               .right  = Side(0.0f),
                                               .bottom = Side(0.0f),
                                               .left   = Side(0.0f),
                                           },
                                           {shadow}));
    ASSERT_EQ(snapped.shadows.size(), 1u);
    EXPECT_FLOAT_EQ(snapped.shadows[0].yDips, 0.8f);
    EXPECT_FLOAT_EQ(snapped.shadows[0].spreadDips, -6.4f);
    EXPECT_FLOAT_EQ(snapped.shadows[0].blurDips, 14.0f);
}

// A shadow box far longer than its edges reach, both ways, so it draws from a bitmap of both ends and one row and
// column between: each edge of the hard shadow still lands on its own pixel.
TEST(Box, AShadowLongerThanItsReachDrawsEachEdgeInPlace) {
    const InsetShadow shadow{
        .xDips      = 0.0f,
        .yDips      = 2.0f,
        .blurDips   = 0.0f,
        .spreadDips = 3.0f,
        .color      = D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f),
    };
    const std::vector<std::uint8_t> pixels = DrawnPixels(Lined(
        Border{
            .top    = Side(0.0f),
            .right  = Side(0.0f),
            .bottom = Side(0.0f),
            .left   = Side(0.0f),
        },
        {shadow}));
    // The shadow's hole is the box moved down 2 and shrunk 3: rows 5 to 28, columns 3 to 56.
    EXPECT_GT(Red(pixels, 30, 4), 200);
    EXPECT_LT(Red(pixels, 30, 5), 50);
    EXPECT_LT(Red(pixels, 30, 28), 50);
    EXPECT_GT(Red(pixels, 30, 29), 200);
    EXPECT_GT(Red(pixels, 2, 15), 200);
    EXPECT_LT(Red(pixels, 3, 15), 50);
    EXPECT_LT(Red(pixels, 56, 15), 50);
    EXPECT_GT(Red(pixels, 57, 15), 200);
}
