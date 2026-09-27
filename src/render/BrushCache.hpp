#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>

// One solid brush per color, made on first use for one device context and kept while it lives.
class BrushCache {
  public:
    explicit BrushCache(ID2D1DeviceContext* context);

    // Throws DeviceLostError when the device is gone.
    ID2D1SolidColorBrush* Get(const D2D1_COLOR_F& color);

  private:
    ID2D1DeviceContext*                                                   _context;
    std::map<std::uint32_t, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> _brushes; // keyed by 0xRRGGBBAA
};
