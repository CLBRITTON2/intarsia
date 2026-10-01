#include "render/Icon.hpp"

#include "platform/Errors.hpp"
#include "platform/Handle.hpp"
#include "render/Graphics.hpp"

#include <d2d1_1.h>
#include <wincodec.h>

#include <cstdint>
#include <format>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace {

    using UniqueBitmap = UniqueReleased<HBITMAP, &DeleteObject>;

    struct IconSize {
        std::uint32_t width;
        std::uint32_t height;
    };

    // Copies the icon's bitmaps, since no call reports the size without them, but spares WIC's copy and its conversion.
    IconSize ReadIconSize(HICON icon) {
        ICONINFO info{};
        if (GetIconInfo(icon, &info) == 0) {
            ThrowLastError("GetIconInfo");
        }
        const UniqueBitmap color(info.hbmColor);
        const UniqueBitmap mask(info.hbmMask);
        BITMAP             bitmap{};
        if (GetObjectW(color != nullptr ? color.get() : mask.get(), sizeof(bitmap), &bitmap) == 0) {
            ThrowLastError("GetObjectW(icon bitmap)");
        }
        // A monochrome icon has no color bitmap, and its mask holds the AND mask above the XOR mask.
        const std::int32_t height = color != nullptr ? bitmap.bmHeight : bitmap.bmHeight / 2;
        return IconSize{.width = static_cast<std::uint32_t>(bitmap.bmWidth), .height = static_cast<std::uint32_t>(height)};
    }

    void RequireReadableSize(std::uint32_t width, std::uint32_t height) {
        if (width == 0 || height == 0 || width > kMaxIconSide || height > kMaxIconSide) {
            throw Win32Error(std::format("icon of {}x{} pixels, the largest read is {}x{}", width, height, kMaxIconSide, kMaxIconSide), E_INVALIDARG);
        }
    }

} // namespace

ComPtr<IWICImagingFactory> CreateImagingFactory() {
    ComPtr<IWICImagingFactory> imaging;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging)), "CoCreateInstance(CLSID_WICImagingFactory)");
    return imaging;
}

IconPixels IconToPixels(IWICImagingFactory& imaging, HICON icon) {
    const IconSize size = ReadIconSize(icon);
    RequireReadableSize(size.width, size.height);
    ComPtr<IWICBitmap> bitmap;
    ThrowIfFailed(imaging.CreateBitmapFromHICON(icon, &bitmap), "IWICImagingFactory::CreateBitmapFromHICON");
    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(imaging.CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                  "IWICFormatConverter::Initialize");

    IconPixels pixels{.width = 0, .height = 0, .pbgra = {}};
    ThrowIfFailed(converter->GetSize(&pixels.width, &pixels.height), "IWICFormatConverter::GetSize");
    RequireReadableSize(pixels.width, pixels.height);
    const std::uint32_t stride = pixels.width * 4;
    pixels.pbgra.resize(static_cast<size_t>(stride) * pixels.height);
    ThrowIfFailed(converter->CopyPixels(nullptr, stride, static_cast<std::uint32_t>(pixels.pbgra.size()), pixels.pbgra.data()), "IWICFormatConverter::CopyPixels");
    return pixels;
}

ComPtr<ID2D1Bitmap> CreateIconBitmap(ID2D1DeviceContext* context, const IconPixels& pixels) {
    ComPtr<ID2D1Bitmap> bitmap;
    ThrowIfDeviceFailed(context->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.pbgra.data(), pixels.width * 4,
                                              D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap),
                        "ID2D1DeviceContext::CreateBitmap(icon)");
    return bitmap;
}
