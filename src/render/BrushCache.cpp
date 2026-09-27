#include "render/BrushCache.hpp"

#include "render/Graphics.hpp"

#include <cmath>
#include <cstdint>

namespace {

    std::uint32_t Channel(float value) {
        return static_cast<std::uint32_t>(std::lround(value * 255.0f)) & 0xFFu;
    }

    std::uint32_t ColorKey(const D2D1_COLOR_F& color) {
        return (Channel(color.r) << 24) | (Channel(color.g) << 16) | (Channel(color.b) << 8) | Channel(color.a);
    }

} // namespace

BrushCache::BrushCache(ID2D1DeviceContext* context) : _context(context) {}

ID2D1SolidColorBrush* BrushCache::Get(const D2D1_COLOR_F& color) {
    const std::uint32_t key   = ColorKey(color);
    const auto          found = _brushes.find(key);
    if (found != _brushes.end()) {
        return found->second.Get();
    }
    return _brushes.emplace(key, CreateBrush(_context, color)).first->second.Get();
}
