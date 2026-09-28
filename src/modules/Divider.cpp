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

std::optional<std::uint32_t> Divider::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool Divider::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void                       Divider::Scroll(ScrollDirection) {}

void                       Divider::Click(D2D1_POINT_2F) {}

void                       Divider::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> Divider::TooltipItem() const {
    return std::nullopt;
}

const ModuleActions& Divider::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> Divider::TooltipText() {
    return std::nullopt;
}
