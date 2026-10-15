#pragma once

#include "config/Config.hpp"

#include <d2d1.h>

#include <chrono>
#include <vector>

// from moved toward to by share, 0 all from and 1 all to.
float Lerp(float from, float to, float share);

// How far a timing function has eased at progress, a share of the duration from 0 to 1, as a share from 0 to 1.
float Eased(TimingFunction timing, float progress);

// A value from 0 to 1 moving to its target over a transition. Retargeted mid-way it turns back from where it is, over
// the share of the duration the distance left takes, as a CSS transition does.
class AnimatedValue {
  public:
    explicit AnimatedValue(float value);

    void  Retarget(float target, const Transition& transition, std::chrono::steady_clock::time_point now);
    float Value(std::chrono::steady_clock::time_point now) const;
    bool  Running(std::chrono::steady_clock::time_point now) const;

  private:
    float                                 _from;
    float                                 _to;
    std::chrono::milliseconds             _duration{0};
    TimingFunction                        _timing = TimingFunction::Linear;
    std::chrono::steady_clock::time_point _start;
};

// A color moving to its target over a transition. Retargeted mid-way it starts again from the color it has reached.
class ColorFade {
  public:
    explicit ColorFade(const D2D1_COLOR_F& color);

    // A target equal to the current one leaves the fade running as it was.
    void         Retarget(const D2D1_COLOR_F& target, const Transition& transition, std::chrono::steady_clock::time_point now);
    D2D1_COLOR_F Color(std::chrono::steady_clock::time_point now) const;
    bool         Running(std::chrono::steady_clock::time_point now) const;

  private:
    D2D1_COLOR_F  _from;
    D2D1_COLOR_F  _to;
    AnimatedValue _progress{1.0f};
};

// Inset shadows moving to a new list over a transition. Shadows pair up by index, and one without a partner fades in
// or out in place, its partner taken as the same shadow in transparent, so a state that adds a shadow fades it in.
class ShadowFade {
  public:
    explicit ShadowFade(const std::vector<InsetShadow>& shadows);

    // A target equal to the current one leaves the fade running as it was.
    void                     Retarget(const std::vector<InsetShadow>& target, const Transition& transition, std::chrono::steady_clock::time_point now);
    std::vector<InsetShadow> Shadows(std::chrono::steady_clock::time_point now) const;
    bool                     Running(std::chrono::steady_clock::time_point now) const;

  private:
    std::vector<InsetShadow> _from;
    std::vector<InsetShadow> _to;
    AnimatedValue            _progress{1.0f};
};

// first and second mixed by share, 0 all first and 1 all second, through premultiplied alpha as CSS mixes them, so a
// fade from transparent never passes through the transparent color's black.
D2D1_COLOR_F MixColors(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second, float share);

// How far a fold has opened and its toggle has turned at one moment.
struct FoldPosition {
    float revealed;     // 0 folded, 1 unfolded
    float angleDegrees; // from the icon rotation folded to the expanded icon one unfolded

    bool  operator==(const FoldPosition&) const = default;
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
    float         _unfoldedDegrees;
    bool          _unfolded = false;
    AnimatedValue _reveal{0.0f};
    AnimatedValue _turn{0.0f};
};
