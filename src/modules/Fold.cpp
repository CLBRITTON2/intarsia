#include "modules/Fold.hpp"

Fold::Fold(const ModuleStyle& style) :
    _revealTransition(style.transition), _turnTransition(style.icon.transition), _foldedDegrees(style.icon.rotationDegrees), _unfoldedDegrees(style.expandedIconRotationDegrees) {}

void Fold::Toggle(std::chrono::steady_clock::time_point now) {
    _unfolded          = !_unfolded;
    const float target = _unfolded ? 1.0f : 0.0f;
    _reveal.Retarget(target, _revealTransition, now);
    _turn.Retarget(target, _turnTransition, now);
}

FoldPosition Fold::At(std::chrono::steady_clock::time_point now) const {
    return FoldPosition{.revealed = _reveal.Value(now), .angleDegrees = Lerp(_foldedDegrees, _unfoldedDegrees, _turn.Value(now))};
}

bool Fold::Running(std::chrono::steady_clock::time_point now) const {
    return _reveal.Running(now) || _turn.Running(now);
}
