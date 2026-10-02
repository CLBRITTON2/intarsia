#include "modules/WindowList.hpp"

#include "render/Icon.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ranges>
#include <utility>

WindowList::WindowList(IWindowManager& windowManager, WindowIcons& icons, HMONITOR monitor, ModuleStyle style) :
    _windowManager(windowManager), _icons(icons), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _style(std::move(style)) {}

bool WindowList::Layout(const RenderContext& render) {
    if (!LayOutItems(render)) {
        return false;
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return true;
}

bool WindowList::LayOutItems(const RenderContext& render) {
    const std::vector<ManagedWindow>               shown = OnMonitor(_windowManager.Windows(), _monitor);
    std::vector<std::shared_ptr<const IconPixels>> pixels;
    pixels.reserve(shown.size());
    for (const ManagedWindow& window : shown) {
        pixels.push_back(_icons.For(window.handle));
    }
    const auto same = [&](size_t index) { return _slots[index].window == shown[index] && _slots[index].pixels == pixels[index]; };
    if (_laidOut && shown.size() == _slots.size() && std::ranges::all_of(std::views::iota(size_t{0}, shown.size()), same)) {
        return false;
    }

    std::vector<Slot> slots;
    slots.reserve(shown.size());
    for (size_t index = 0; index < shown.size(); ++index) {
        // A bitmap already made from the same pixels is kept: making one costs an upload to the GPU.
        const auto kept = std::ranges::find_if(_slots, [&](const Slot& slot) { return slot.window.handle == shown[index].handle && slot.pixels == pixels[index]; });
        auto bitmap     = kept != _slots.end() ? kept->bitmap : (pixels[index] != nullptr && pixels[index]->width > 0 ? CreateIconBitmap(render.context, *pixels[index]) : nullptr);
        slots.push_back(Slot{.window = shown[index], .pixels = pixels[index], .bitmap = std::move(bitmap)});
    }
    _slots   = std::move(slots);
    _laidOut = true;

    const float icon = _style.icon.sizeDips;
    _row             = ArrangeItems(std::vector<D2D1_SIZE_F>(_slots.size(), D2D1_SIZE_F{.width = icon, .height = icon}), ItemStyles(), render.axis);
    return true;
}

D2D1_SIZE_F WindowList::Size() const {
    return _row.size;
}

void WindowList::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const float                                 icon = _style.icon.sizeDips;
    const std::chrono::steady_clock::time_point now  = std::chrono::steady_clock::now();
    for (size_t index = 0; index < _slots.size(); ++index) {
        const Slot&      slot  = _slots[index];
        const ItemStyle& style = StyleOf(slot);
        DrawItemBackground(render, _row.boxes[index], style, _fades.Background(index, now), _fades.Shadows(index, now), origin);
        if (slot.bitmap != nullptr) {
            const D2D1_POINT_2F start = ItemContentOrigin(_row.boxes[index], style, origin);
            render.context->DrawBitmap(slot.bitmap.Get(), D2D1::RectF(start.x, start.y, start.x + icon, start.y + icon), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }
}

const ItemStyle& WindowList::StyleOf(const Slot& slot) const {
    return slot.window.focused ? _style.focused : _style.item;
}

std::vector<const ItemStyle*> WindowList::ItemStyles() const {
    std::vector<const ItemStyle*> styles;
    styles.reserve(_slots.size());
    for (const Slot& slot : _slots) {
        styles.push_back(&StyleOf(slot));
    }
    return styles;
}

std::optional<std::uint32_t> WindowList::MillisecondsUntilChange() const {
    return _fades.Wait(std::chrono::steady_clock::now());
}

bool WindowList::Hover(std::optional<D2D1_POINT_2F> point) {
    const bool changed = _hovered.Set(point.has_value() ? ItemAt(_row, *point) : std::nullopt);
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

void WindowList::Click(D2D1_POINT_2F point) {
    const std::optional<size_t> index = ItemAt(_row, point);
    if (index.has_value()) {
        _windowManager.FocusWindow(_slots[*index].window.id);
    }
}

const ModuleActions& WindowList::Actions() const {
    return kNoActions;
}
