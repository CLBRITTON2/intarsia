#pragma once

#include <d2d1_1.h>
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

// The icon's pixels. Throws Win32Error when WIC cannot read it. The calling thread needs a COM apartment.
IconPixels IconToPixels(HICON icon);

// The pixels uploaded to context's device. Throws DeviceLostError when the device is gone.
Microsoft::WRL::ComPtr<ID2D1Bitmap> CreateIconBitmap(ID2D1DeviceContext* context, const IconPixels& pixels);
