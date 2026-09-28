#pragma once

#include "config/Config.hpp"

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
