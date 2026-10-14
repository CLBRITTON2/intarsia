#include "modules/BindingModes.hpp"

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

BindingModes::BindingModes(const IWindowManager& windowManager, ModuleStyle style) : _windowManager(windowManager), _style(std::move(style)) {}

bool BindingModes::Layout(const RenderContext& render) {
    if (!LayOutItems(render)) {
        return false;
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return true;
}

bool BindingModes::LayOutItems(const RenderContext& render) {
    std::vector<std::wstring> modes = _windowManager.BindingModes();
    if (_laidOut && modes == _shown) {
        return false;
    }
    _shown   = std::move(modes);
    _laidOut = true;
    _labels.clear();
    std::vector<D2D1_SIZE_F> contents;
    for (const std::wstring& mode : _shown) {
        _labels.push_back(LayOutWeightedText(render, mode, _style.item.weight, ItemTextInset(_style.item, render.axis)));
        contents.push_back(_labels.back().size);
    }
    _row = ArrangeItems(contents, ItemStyles(), render.axis);
    return true;
}

std::vector<const ItemStyle*> BindingModes::ItemStyles() const {
    return std::vector<const ItemStyle*>(_labels.size(), &_style.item);
}

D2D1_SIZE_F BindingModes::Size() const {
    return _row.size;
}

void BindingModes::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const ItemStyle& itemStyle = _style.item;
    for (size_t index = 0; index < _labels.size(); ++index) {
        DrawItemBackground(render, _row.boxes[index], itemStyle, _fades.Background(index, render.frameTime), _fades.Shadows(index, render.frameTime), origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], itemStyle, origin), _labels[index], render.accent, render.brushes->Get(_fades.Text(index, render.frameTime)));
    }
}

std::optional<std::uint32_t> BindingModes::MillisecondsUntilChange() const {
    return _fades.Wait(std::chrono::steady_clock::now());
}

bool BindingModes::Hover(std::optional<D2D1_POINT_2F> point) {
    const bool changed = _hovered.Set(ItemUnderCursor(_row, point));
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

const ModuleActions& BindingModes::Actions() const {
    return kNoActions;
}
