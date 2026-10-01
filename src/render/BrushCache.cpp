#include "render/BrushCache.hpp"

#include "render/Graphics.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

    std::uint32_t Channel(float value) {
        return static_cast<std::uint32_t>(std::lround(value * 255.0f)) & 0xFFu;
    }

    std::uint32_t ColorKey(const D2D1_COLOR_F& color) {
        return (Channel(color.r) << 24) | (Channel(color.g) << 16) | (Channel(color.b) << 8) | Channel(color.a);
    }

    // A few fades' worth of frames. Releasing a brush mid-frame is safe: the context holds its own reference for
    // pending draws.
    constexpr size_t kMaxPassingBrushes = 256;

} // namespace

BrushCache::BrushCache(ID2D1DeviceContext* context) : _context(context) {}

ID2D1SolidColorBrush* BrushCache::Get(const D2D1_COLOR_F& color) {
    const std::uint32_t key = ColorKey(color);
    if (const auto kept = _kept.find(key); kept != _kept.end()) {
        return kept->second.Get();
    }
    if (const auto found = _passing.find(key); found != _passing.end()) {
        return found->second.Get();
    }
    if (_passing.size() >= kMaxPassingBrushes) {
        _passing.clear();
    }
    return _passing.emplace(key, CreateBrush(_context, color)).first->second.Get();
}

ID2D1SolidColorBrush* BrushCache::Keep(const D2D1_COLOR_F& color) {
    const std::uint32_t key   = ColorKey(color);
    const auto          found = _kept.find(key);
    if (found != _kept.end()) {
        return found->second.Get();
    }
    return _kept.emplace(key, CreateBrush(_context, color)).first->second.Get();
}
