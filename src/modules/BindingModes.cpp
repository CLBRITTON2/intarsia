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
    ID2D1SolidColorBrush* text = render.brushes->Get(_style.item.color);
    for (size_t index = 0; index < _labels.size(); ++index) {
        DrawItemBackground(render, _row.boxes[index], _style.item, origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], _style.item, origin), _labels[index], text);
    }
}

std::optional<std::uint32_t> BindingModes::MillisecondsUntilChange() const {
    return std::nullopt;
}

void BindingModes::Scroll(ScrollDirection) {}

void                 BindingModes::Click(D2D1_POINT_2F) {}

const ModuleActions& BindingModes::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> BindingModes::TooltipText() {
    return std::nullopt;
}
