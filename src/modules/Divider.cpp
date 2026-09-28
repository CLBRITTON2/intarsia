#include "modules/Divider.hpp"

#include <cstdint>
#include <utility>

Divider::Divider(ModuleStyle style) : _style(std::move(style)) {}

bool Divider::Layout(const RenderContext& render) {
    if (_laidOut && render.boxHeightDips == _boxHeightDips) {
        return false;
    }
    _boxHeightDips = render.boxHeightDips;
    _laidOut       = true;
    return true;
}

D2D1_SIZE_F Divider::Size() const {
    return D2D1_SIZE_F{.width = _style.lineWidthDips, .height = _boxHeightDips * _style.lineHeight};
}

void Divider::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const D2D1_SIZE_F size = Size();
    render.context->FillRectangle(D2D1::RectF(origin.x, origin.y, origin.x + size.width, origin.y + size.height), render.foreground);
}

const ModuleActions& Divider::Actions() const {
    return kNoActions;
}
