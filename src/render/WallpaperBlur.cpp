#include "render/WallpaperBlur.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"
#include "render/Box.hpp"
#include "render/Composition.hpp"
#include "render/Icon.hpp"

#include <d2d1_1helper.h>
#include <d2d1effects.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace {

    // The copy of the desktop a bar keeps is downscaled as far as leaves the blur 2 of its pixels or more, which hides
    // them. At most 8 times, past which stretching it back over the monitor shows bands.
    constexpr std::uint32_t kLargestDownscale = 8;

    float                   DeviationPixels(const Backdrop& backdrop, std::uint32_t dpi) {
        return backdrop.radiusDips * static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    }

    std::uint32_t DownscaleFor(float deviationPixels) {
        return std::clamp(static_cast<std::uint32_t>(deviationPixels / 2.0f), std::uint32_t{1}, kLargestDownscale);
    }

    // Rounded up, so the copy covers the monitor's last partial pixel.
    std::uint32_t DownscaledLength(std::int32_t pixels, std::uint32_t downscale) {
        return (static_cast<std::uint32_t>(pixels) + downscale - 1) / downscale;
    }

    ComPtr<ID2D1Bitmap1> CreateTargetBitmap(ID2D1DeviceContext* context, D2D1_SIZE_U size, std::string_view purpose) {
        const D2D1_BITMAP_PROPERTIES1 properties =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfDeviceFailed(context->CreateBitmap(size, nullptr, 0, &properties, &bitmap), std::format("ID2D1DeviceContext::CreateBitmap({})", purpose));
        return bitmap;
    }

    // The wallpaper image's place on the desktop's copy, in the copy's pixels.
    struct PlacedImage {
        std::int64_t left;
        std::int64_t top;
        std::int64_t width; // at least 1
        std::int64_t height;
    };

    PlacedImage Downscaled(const RECT& imageRect, const RECT& monitorRect, std::uint32_t downscale) {
        const auto scale = static_cast<double>(downscale);
        return PlacedImage{
            .left   = std::llround(static_cast<double>(imageRect.left - monitorRect.left) / scale),
            .top    = std::llround(static_cast<double>(imageRect.top - monitorRect.top) / scale),
            .width  = std::max(std::llround(static_cast<double>(imageRect.right - imageRect.left) / scale), std::int64_t{1}),
            .height = std::max(std::llround(static_cast<double>(imageRect.bottom - imageRect.top) / scale), std::int64_t{1}),
        };
    }

    // The part of a placed image that lands on the copy: source in the scaled image's pixels, target in the copy's.
    struct ImageSlice {
        WICRect     source;
        D2D1_RECT_F target;
    };

    // None when the image lands wholly off a copy of size.
    std::optional<ImageSlice> SliceOn(const PlacedImage& image, D2D1_SIZE_U size) {
        const std::int64_t left   = std::max(image.left, std::int64_t{0});
        const std::int64_t top    = std::max(image.top, std::int64_t{0});
        const std::int64_t right  = std::min(image.left + image.width, static_cast<std::int64_t>(size.width));
        const std::int64_t bottom = std::min(image.top + image.height, static_cast<std::int64_t>(size.height));
        if (right <= left || bottom <= top) {
            return std::nullopt;
        }
        return ImageSlice{
            .source =
                WICRect{
                    .X      = static_cast<INT>(left - image.left),
                    .Y      = static_cast<INT>(top - image.top),
                    .Width  = static_cast<INT>(right - left),
                    .Height = static_cast<INT>(bottom - top),
                },
            .target = D2D1::RectF(static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)),
        };
    }

    // Draws image on the copy of size as the desktop lays it on the monitor, downscale monitor pixels to each of the
    // copy's.
    void DrawImage(ID2D1DeviceContext* context, IWICBitmapSource* image, const Wallpaper& wallpaper, const RECT& monitorRect, std::uint32_t downscale, D2D1_SIZE_U size) {
        D2D1_SIZE_U imageSize{};
        ThrowIfFailed(image->GetSize(&imageSize.width, &imageSize.height), "IWICBitmapSource::GetSize(wallpaper)");
        if (imageSize.width == 0 || imageSize.height == 0) {
            throw WallpaperError(std::format("the wallpaper {} is {}x{} pixels", ToUtf8(wallpaper.image.wstring()), imageSize.width, imageSize.height));
        }
        const PlacedImage               placed = Downscaled(PlaceWallpaper(wallpaper.fit, imageSize, monitorRect, wallpaper.desktopRect), monitorRect, downscale);
        const std::optional<ImageSlice> slice  = SliceOn(placed, size);
        if (!slice.has_value()) {
            return;
        }
        // Scaled by WIC rather than D2D, which averages every source pixel into a far smaller copy, and clipped to the
        // part that shows, so a fill of a tall image on a wide monitor never outgrows the largest bitmap.
        const ComPtr<IWICImagingFactory> imaging = CreateImagingFactory();
        ComPtr<IWICBitmapScaler>         scaler;
        ThrowIfFailed(imaging->CreateBitmapScaler(&scaler), "IWICImagingFactory::CreateBitmapScaler");
        ThrowIfFailed(scaler->Initialize(image, static_cast<UINT>(placed.width), static_cast<UINT>(placed.height), WICBitmapInterpolationModeFant), "IWICBitmapScaler::Initialize");
        ComPtr<IWICBitmapClipper> clipper;
        ThrowIfFailed(imaging->CreateBitmapClipper(&clipper), "IWICImagingFactory::CreateBitmapClipper");
        ThrowIfFailed(clipper->Initialize(scaler.Get(), &slice->source), "IWICBitmapClipper::Initialize");
        ComPtr<ID2D1Bitmap> bitmap;
        ThrowIfDeviceFailed(context->CreateBitmapFromWicBitmap(clipper.Get(), &bitmap), "ID2D1DeviceContext::CreateBitmapFromWicBitmap(wallpaper)");
        if (wallpaper.fit != WallpaperFit::Tile) {
            context->DrawBitmap(bitmap.Get(), &slice->target, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr, nullptr);
            return;
        }
        ComPtr<ID2D1BitmapBrush1> tiles;
        ThrowIfDeviceFailed(
            context->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR), &tiles),
            "ID2D1DeviceContext::CreateBitmapBrush(wallpaper tiles)");
        context->FillRectangle(D2D1::RectF(0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height)), tiles.Get());
    }

    ComPtr<ID2D1Effect> CreateEffect(ID2D1DeviceContext* context, const CLSID& effect, std::string_view name) {
        ComPtr<ID2D1Effect> created;
        ThrowIfDeviceFailed(context->CreateEffect(effect, &created), std::format("ID2D1DeviceContext::CreateEffect({})", name));
        return created;
    }

    template <typename Value>
    void SetProperty(ID2D1Effect* effect, std::uint32_t index, const Value& value, std::string_view name) {
        ThrowIfFailed(effect->SetValue(index, value), std::format("ID2D1Effect::SetValue({})", name));
    }

    // The monitor's desktop at a downscale'th of its size, blurred at deviation (in the copy's pixels) and toned by
    // backdrop. Its own context draws it, at 96 DPI so a DIP is one of its pixels.
    ComPtr<ID2D1Bitmap1> BlurDesktop(const GraphicsDevice& graphics, const Wallpaper& wallpaper, IWICBitmapSource* image, const RECT& monitorRect, std::uint32_t downscale,
                                     float deviation, const Backdrop& backdrop) {
        ComPtr<ID2D1DeviceContext> context;
        ThrowIfDeviceFailed(graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context), "ID2D1Device::CreateDeviceContext(wallpaper)");
        const D2D1_SIZE_U size = D2D1::SizeU(DownscaledLength(monitorRect.right - monitorRect.left, downscale), DownscaledLength(monitorRect.bottom - monitorRect.top, downscale));

        const ComPtr<ID2D1Bitmap1> desktop = CreateTargetBitmap(context.Get(), size, "wallpaper desktop");
        context->SetTarget(desktop.Get());
        context->BeginDraw();
        context->Clear(wallpaper.background);
        if (image != nullptr) {
            DrawImage(context.Get(), image, wallpaper, monitorRect, downscale, size);
        }
        ThrowIfDeviceFailed(context->EndDraw(), "ID2D1DeviceContext::EndDraw(wallpaper desktop)");

        // The border repeats the copy's edge pixels outward, so the blur darkens no edge with the transparency past it.
        const ComPtr<ID2D1Effect> border = CreateEffect(context.Get(), CLSID_D2D1Border, "border");
        border->SetInput(0, desktop.Get());
        SetProperty(border.Get(), D2D1_BORDER_PROP_EDGE_MODE_X, D2D1_BORDER_EDGE_MODE_CLAMP, "border edge x");
        SetProperty(border.Get(), D2D1_BORDER_PROP_EDGE_MODE_Y, D2D1_BORDER_EDGE_MODE_CLAMP, "border edge y");
        const ComPtr<ID2D1Effect> blur = CreateEffect(context.Get(), CLSID_D2D1GaussianBlur, "gaussian blur");
        blur->SetInputEffect(0, border.Get());
        SetProperty(blur.Get(), D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, deviation, "blur deviation");
        const ComPtr<ID2D1Effect> tone = CreateEffect(context.Get(), CLSID_D2D1ColorMatrix, "color matrix");
        tone->SetInputEffect(0, blur.Get());
        SetProperty(tone.Get(), D2D1_COLORMATRIX_PROP_COLOR_MATRIX, std::bit_cast<D2D1_MATRIX_5X4_F>(BackdropColorMatrix(backdrop)), "color matrix");
        SetProperty(tone.Get(), D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED, "color matrix alpha mode");
        SetProperty(tone.Get(), D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, BOOL{TRUE}, "color matrix clamp");

        const ComPtr<ID2D1Bitmap1> blurred = CreateTargetBitmap(context.Get(), size, "wallpaper blur");
        context->SetTarget(blurred.Get());
        context->BeginDraw();
        context->DrawImage(tone.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        ThrowIfDeviceFailed(context->EndDraw(), "ID2D1DeviceContext::EndDraw(wallpaper blur)");
        context->SetTarget(nullptr);
        return blurred;
    }

    // Null for a wallpaper of a solid color.
    ComPtr<IWICBitmapSource> DecodedImage(const Wallpaper& wallpaper) {
        if (wallpaper.image.empty()) {
            return nullptr;
        }
        const ComPtr<IWICImagingFactory> imaging = CreateImagingFactory();
        return DecodeWallpaper(*imaging.Get(), wallpaper.image);
    }

} // namespace

WallpaperBlur::WallpaperBlur(const GraphicsDevice& graphics, const Wallpaper& wallpaper, IWICBitmapSource* image, const RECT& monitorRect, std::uint32_t dpi,
                             const Backdrop& backdrop) :
    _backdrop(backdrop), _monitorRect(monitorRect), _downscale(DownscaleFor(DeviationPixels(backdrop, dpi))),
    _blurred(BlurDesktop(graphics, wallpaper, image, monitorRect, _downscale, DeviationPixels(backdrop, dpi) / static_cast<float>(_downscale), backdrop)) {}

const Backdrop& WallpaperBlur::BackdropOf() const {
    return _backdrop;
}

void WallpaperBlur::Fill(ID2D1DeviceContext* context, const RECT& windowRect, const std::vector<D2D1_ROUNDED_RECT>& outlines) const {
    if (outlines.empty()) {
        return;
    }
    // From the blur's pixels to the window's DIPs: each spans _downscale monitor pixels, from the monitor's top left.
    const float             pixelsPerDip = PixelGridOf(context).pixelsPerDip;
    const float             scale        = static_cast<float>(_downscale) / pixelsPerDip;
    const D2D1_MATRIX_3X2_F placement    = D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(static_cast<float>(_monitorRect.left - windowRect.left) / pixelsPerDip, static_cast<float>(_monitorRect.top - windowRect.top) / pixelsPerDip);
    ComPtr<ID2D1BitmapBrush1> brush;
    ThrowIfDeviceFailed(context->CreateBitmapBrush(_blurred.Get(), D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_INTERPOLATION_MODE_LINEAR),
                                                   D2D1::BrushProperties(1.0f, placement), &brush),
                        "ID2D1DeviceContext::CreateBitmapBrush(wallpaper blur)");
    for (const D2D1_ROUNDED_RECT& outline : outlines) {
        context->FillRoundedRectangle(outline, brush.Get());
    }
}

ComPtr<IWICBitmapSource> DecodeWallpaper(IWICImagingFactory& imaging, const std::filesystem::path& image) {
    const std::string         name = ToUtf8(image.wstring());
    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(imaging.CreateDecoderFromFilename(image.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder),
                  std::format("IWICImagingFactory::CreateDecoderFromFilename({})", name));
    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame), std::format("IWICBitmapDecoder::GetFrame({})", name));
    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(imaging.CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                  std::format("IWICFormatConverter::Initialize({})", name));
    return converter;
}

std::vector<WallpaperBlur> BlurWallpapers(const GraphicsDevice& graphics, const StyleConfig& style, const RECT& monitorRect, std::uint32_t dpi) {
    std::vector<Backdrop> backdrops;
    for (const Backdrop* backdrop : std::array{&style.section.backdrop, &style.tooltip.backdrop, &style.menu.backdrop}) {
        if (backdrop->source == BackdropSource::Wallpaper && std::ranges::find(backdrops, *backdrop) == backdrops.end()) {
            backdrops.push_back(*backdrop);
        }
    }
    std::vector<WallpaperBlur> blurs;
    if (backdrops.empty()) {
        return blurs;
    }
    const Wallpaper                wallpaper = ReadWallpaper(monitorRect);
    const ComPtr<IWICBitmapSource> image     = DecodedImage(wallpaper);
    blurs.reserve(backdrops.size());
    for (const Backdrop& backdrop : backdrops) {
        blurs.emplace_back(graphics, wallpaper, image.Get(), monitorRect, dpi, backdrop);
    }
    return blurs;
}

const WallpaperBlur& BlurFor(const std::vector<WallpaperBlur>& blurs, const Backdrop& backdrop) {
    const auto found = std::ranges::find(blurs, backdrop, &WallpaperBlur::BackdropOf);
    if (found == blurs.end()) {
        throw std::logic_error(std::format("no wallpaper blur was made for the backdrop of radius {}, brightness {} and saturation {}", backdrop.radiusDips, backdrop.brightness,
                                           backdrop.saturation));
    }
    return *found;
}
