#include "modules/Group.hpp"

#include <chrono>

Group::Group(const GroupConfig& config, const ModuleStyle& style) : _icon(config.icon), _fold(style), _toggle(EmptyIconText()) {}

bool Group::Layout(const RenderContext& render) {
    const FoldPosition position = _fold.At(render.frameTime);
    const bool changed = !_laidOut || position != _shown;
    _shown = position;
    if (!_laidOut) {
        _toggle = LayOutIconText(render, _icon, L"");
    }
    _laidOut = true;
    return changed;
}

D2D1_SIZE_F Group::Size() const {
    return _toggle.size;
}

void Group::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawTurnedIconText(render, origin, _toggle, _shown.angleDegrees, render.accent, render.foreground);
}

std::optional<std::uint32_t> Group::MillisecondsUntilChange() const {
    if (_fold.Running(std::chrono::steady_clock::now())) {
        return 0;
    }
    return std::nullopt;
}

void Group::Click(D2D1_POINT_2F) {
    _fold.Toggle(std::chrono::steady_clock::now());
}

const ModuleActions& Group::Actions() const {
    return kNoActions;
}

float Group::Revealed() const {
    return _shown.revealed;
}
