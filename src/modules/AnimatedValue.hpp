#pragma once

#include "config/Config.hpp"

#include <d2d1.h>

#include <chrono>

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

// first and second mixed by share, 0 all first and 1 all second, through premultiplied alpha as CSS mixes them, so a
// fade from transparent never passes through the transparent color's black.
D2D1_COLOR_F MixColors(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second, float share);
