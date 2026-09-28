#include "modules/BindingModes.hpp"

#include <cstdint>
#include <utility>

BindingModes::BindingModes(const IWindowManager& windowManager, ModuleStyle style) : _windowManager(windowManager), _style(std::move(style)) {}

bool BindingModes::Layout(const RenderContext& render) {
    std::vector<std::wstring> modes = _windowManager.BindingModes();
    if (_laidOut && modes == _shown) {
        return false;
    }
    _shown   = std::move(modes);
    _laidOut = true;
    _labels.clear();
    std::vector<D2D1_SIZE_F> contents;
    for (const std::wstring& mode : _shown) {
        _labels.push_back(LayOutWeightedText(render, mode, _style.item.weight));
        contents.push_back(_labels.back().size);
    }
    _row = ArrangeItems(contents, std::vector<const ItemStyle*>(_labels.size(), &_style.item));
    return true;
}

D2D1_SIZE_F BindingModes::Size() const {
    return _row.size;
}

void BindingModes::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const ItemStyle& item = _style.item;
    for (size_t index = 0; index < _labels.size(); ++index) {
        const bool hovered = _hovered == index;
        DrawItemBackground(render, _row.boxes[index], item, hovered ? item.hoverBackground : item.background, origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], item, origin), _labels[index], render.brushes->Get(hovered ? item.hoverColor : item.color));
    }
}

std::optional<std::uint32_t> BindingModes::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool BindingModes::Hover(std::optional<D2D1_POINT_2F> point) {
    const std::optional<size_t> hovered = point.has_value() ? ItemAt(_row, *point) : std::nullopt;
    const bool                  changed = hovered != _hovered;
    _hovered                            = hovered;
    return changed;
}

void                       BindingModes::Scroll(ScrollDirection) {}

void                       BindingModes::Click(D2D1_POINT_2F) {}

void                       BindingModes::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> BindingModes::TooltipItem() const {
    return std::nullopt;
}

const ModuleActions& BindingModes::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> BindingModes::TooltipText() {
    return std::nullopt;
}
