#include "modules/Fold.hpp"

Fold::Fold(const ModuleStyle& style) :
    _revealTransition(style.transition), _turnTransition(style.icon.transition), _foldedDegrees(style.icon.rotationDegrees), _expandedDegrees(style.expandedIconRotationDegrees) {}

void Fold::Toggle(std::chrono::steady_clock::time_point now) {
    _expanded          = !_expanded;
    const float target = _expanded ? 1.0f : 0.0f;
    _reveal.Retarget(target, _revealTransition, now);
    _turn.Retarget(target, _turnTransition, now);
}

FoldPosition Fold::At(std::chrono::steady_clock::time_point now) const {
    return FoldPosition{.reveal = _reveal.Value(now), .angleDegrees = _foldedDegrees + ((_expandedDegrees - _foldedDegrees) * _turn.Value(now))};
}

bool Fold::Running(std::chrono::steady_clock::time_point now) const {
    return _reveal.Running(now) || _turn.Running(now);
}
