#pragma once

#include "config/Config.hpp"
#include "modules/AnimatedValue.hpp"

#include <chrono>

// How far a fold has opened and its toggle has turned at one moment.
struct FoldPosition {
    float reveal;       // 0 folded, 1 unfolded
    float angleDegrees; // from the icon rotation folded to the expanded icon one unfolded
};

// What a toggle folds away and unfolds again, starting folded. It unfolds over the module's width transition and its
// toggle turns over the icon's. Toggled mid-way it turns back from where it is, as CSS reverses a transition.
class Fold {
  public:
    explicit Fold(const ModuleStyle& style);

    void         Toggle(std::chrono::steady_clock::time_point now);
    FoldPosition At(std::chrono::steady_clock::time_point now) const;
    bool         Running(std::chrono::steady_clock::time_point now) const;

  private:
    Transition    _revealTransition;
    Transition    _turnTransition;
    float         _foldedDegrees;
    float         _expandedDegrees;
    bool          _expanded = false;
    AnimatedValue _reveal{0.0f};
    AnimatedValue _turn{0.0f};
};
