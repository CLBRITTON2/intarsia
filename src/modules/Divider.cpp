#include "modules/Divider.hpp"

#include <cstdint>
#include <utility>

Divider::Divider(ModuleStyle style) : _style(std::move(style)) {}

bool Divider::Layout(const RenderContext& render) {
    if (_laidOut && render.boxCrossDips == _boxCrossDips && render.axis == _axis) {
        return false;
    }
    _boxCrossDips = render.boxCrossDips;
    _axis         = render.axis;
    _laidOut      = true;
    return true;
}

D2D1_SIZE_F Divider::Size() const {
    return MainCrossSize(_axis, _style.lineWidthDips, _boxCrossDips * _style.lineHeight);
}

void Divider::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const D2D1_SIZE_F size = Size();
    render.context->FillRectangle(D2D1::RectF(origin.x, origin.y, origin.x + size.width, origin.y + size.height), render.foreground);
}

const ModuleActions& Divider::Actions() const {
    return kNoActions;
}
