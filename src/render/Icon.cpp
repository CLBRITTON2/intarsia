#include "render/Icon.hpp"

#include "platform/Errors.hpp"
#include "render/Graphics.hpp"

#include <d2d1_1.h>
#include <wincodec.h>

#include <cstdint>
#include <format>

using Microsoft::WRL::ComPtr;

IconPixels IconToPixels(HICON icon) {
    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance(CLSID_WICImagingFactory)");
    ComPtr<IWICBitmap> bitmap;
    ThrowIfFailed(factory->CreateBitmapFromHICON(icon, &bitmap), "IWICImagingFactory::CreateBitmapFromHICON");
    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                  "IWICFormatConverter::Initialize");

    IconPixels pixels{.width = 0, .height = 0, .pbgra = {}};
    ThrowIfFailed(converter->GetSize(&pixels.width, &pixels.height), "IWICFormatConverter::GetSize");
    if (pixels.width == 0 || pixels.height == 0 || pixels.width > kMaxIconSide || pixels.height > kMaxIconSide) {
        throw Win32Error(std::format("icon of {}x{} pixels, the largest read is {}x{}", pixels.width, pixels.height, kMaxIconSide, kMaxIconSide), E_INVALIDARG);
    }
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
