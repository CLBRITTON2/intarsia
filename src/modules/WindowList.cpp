#include "modules/WindowList.hpp"

#include "render/Graphics.hpp"

#include <cstdint>
#include <algorithm>
#include <iterator>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

    // Half an icon between icons, as the two spaces between workspace labels are about half a line.
    constexpr float kGapPerIconSize = 0.5f;
    // Strength of the icons of windows that do not have focus.
    constexpr float     kDimmedOpacity = 0.5f;

    ComPtr<ID2D1Bitmap> CreateIconBitmap(ID2D1DeviceContext* context, const IconPixels& pixels) {
        ComPtr<ID2D1Bitmap> bitmap;
        ThrowIfDeviceFailed(context->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.pbgra.data(), pixels.width * 4,
                                                  D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap),
                            "ID2D1DeviceContext::CreateBitmap(window icon)");
        return bitmap;
    }

} // namespace

WindowList::WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor) :
    _windowManager(windowManager), _icons(icons), _monitor(reinterpret_cast<std::intptr_t>(monitor)) {}

bool WindowList::Layout(const RenderContext& render) {
    const std::vector<ManagedWindow> shown   = WindowsOnMonitor(_windowManager.Windows(), _monitor);
    bool                             changed = !_laidOut || shown.size() != _slots.size() || render.iconSizeDips != _iconSizeDips;
    _iconSizeDips                            = render.iconSizeDips;
    std::vector<Slot> slots;
    slots.reserve(shown.size());
    for (size_t index = 0; index < shown.size(); ++index) {
        std::shared_ptr<const IconPixels> pixels = _icons.For(shown[index].handle);
        // A bitmap already made from the same pixels is kept: making one costs an upload to the GPU.
        const auto kept   = std::ranges::find_if(_slots, [&](const Slot& slot) { return slot.window.handle == shown[index].handle && slot.pixels == pixels; });
        auto       bitmap = kept != _slots.end() ? kept->bitmap : (pixels != nullptr && pixels->width > 0 ? CreateIconBitmap(render.context, *pixels) : nullptr);
        // changed is already true when the counts differ, so _slots[index] is only read while it exists.
        changed = changed || !(_slots[index].window == shown[index]) || _slots[index].pixels != pixels;
        slots.push_back(Slot{.window = shown[index], .pixels = std::move(pixels), .bitmap = std::move(bitmap)});
    }
    _slots   = std::move(slots);
    _laidOut = true;
    return changed;
}

D2D1_SIZE_F WindowList::Size() const {
    if (_slots.empty()) {
        return D2D1_SIZE_F{.width = 0.0f, .height = 0.0f};
    }
    const auto count = static_cast<float>(_slots.size());
    return D2D1_SIZE_F{.width = (count * _iconSizeDips) + ((count - 1.0f) * _iconSizeDips * kGapPerIconSize), .height = _iconSizeDips};
}

void WindowList::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const float step = _iconSizeDips * (1.0f + kGapPerIconSize);
    for (size_t index = 0; index < _slots.size(); ++index) {
        const Slot& slot = _slots[index];
        if (slot.bitmap == nullptr) {
            continue;
        }
        const float left = origin.x + (static_cast<float>(index) * step);
        render.context->DrawBitmap(slot.bitmap.Get(), D2D1::RectF(left, origin.y, left + _iconSizeDips, origin.y + _iconSizeDips), slot.window.focused ? 1.0f : kDimmedOpacity,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

std::optional<std::uint32_t> WindowList::MillisecondsUntilChange() const {
    return std::nullopt;
}

void WindowList::Click(D2D1_POINT_2F point) {
    const std::optional<size_t> index = IconAt(point.x, _slots.size(), _iconSizeDips, _iconSizeDips * kGapPerIconSize);
    if (index.has_value()) {
        _windowManager.FocusWindow(_slots[*index].window.id);
    }
}

const ModuleActions& WindowList::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> WindowList::TooltipText() {
    return std::nullopt;
}

std::vector<ManagedWindow> WindowsOnMonitor(const std::vector<ManagedWindow>& windows, std::intptr_t monitor) {
    std::vector<ManagedWindow> shown;
    std::ranges::copy_if(windows, std::back_inserter(shown), [monitor](const ManagedWindow& window) { return window.monitor == monitor; });
    return shown;
}

std::optional<size_t> IconAt(float x, size_t count, float iconSize, float gap) {
    if (x < 0.0f) {
        return std::nullopt;
    }
    const float step  = iconSize + gap;
    const auto  index = static_cast<size_t>(x / step);
    const float into  = x - (static_cast<float>(index) * step);
    if (index >= count || into >= iconSize) {
        return std::nullopt;
    }
    return index;
}
