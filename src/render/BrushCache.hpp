#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>

// Solid brushes by color, made on first use for one device context.
class BrushCache {
  public:
    explicit BrushCache(ID2D1DeviceContext* context);

    // A brush to draw with at once: fades ask for a new color each frame, so these start over past a limit, and a
    // pointer from an earlier call may be gone. Throws DeviceLostError when the device is gone.
    ID2D1SolidColorBrush* Get(const D2D1_COLOR_F& color);
    // A brush kept while the cache lives, for a pointer held across frames. Throws DeviceLostError when the device is
    // gone.
    ID2D1SolidColorBrush* Keep(const D2D1_COLOR_F& color);

  private:
    using Brushes = std::map<std::uint32_t, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>>; // keyed by 0xRRGGBBAA

    ID2D1DeviceContext* _context;
    Brushes             _passing;
    Brushes             _kept;
};
