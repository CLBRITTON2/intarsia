#pragma once

#include <d2d1_1.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

// An icon's pixels, premultiplied BGRA with no row padding, ready for ID2D1DeviceContext::CreateBitmap.
struct IconPixels {
    std::uint32_t             width;
    std::uint32_t             height;
    std::vector<std::uint8_t> pbgra;

    bool                      operator==(const IconPixels&) const = default;
};

// The largest icon read, per side. Any process can hand the tray an icon of any size, and every bar keeps a copy on
// its device, while a bar draws icons at a font size.
inline constexpr std::uint32_t kMaxIconSide = 256;

// A WIC factory for IconToPixels, made once per thread that reads icons and used only there. The thread needs a COM
// apartment for as long as the factory lives.
Microsoft::WRL::ComPtr<IWICImagingFactory> CreateImagingFactory();

// The icon's pixels. Throws Win32Error when WIC cannot read it or it is empty or larger than kMaxIconSide.
IconPixels IconToPixels(IWICImagingFactory& imaging, HICON icon);

// The pixels uploaded to context's device. Throws DeviceLostError when the device is gone.
Microsoft::WRL::ComPtr<ID2D1Bitmap> CreateIconBitmap(ID2D1DeviceContext* context, const IconPixels& pixels);
