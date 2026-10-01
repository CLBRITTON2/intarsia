#include "modules/AnimatedValue.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>

namespace {

    struct Bezier {
        float x1;
        float y1;
        float x2;
        float y2;
    };

    // The control points CSS Easing Functions Level 1 gives each keyword.
    Bezier CurveOf(TimingFunction timing) {
        switch (timing) {
            case TimingFunction::Linear: return {.x1 = 0.0f, .y1 = 0.0f, .x2 = 1.0f, .y2 = 1.0f};
            case TimingFunction::Ease: return {.x1 = 0.25f, .y1 = 0.1f, .x2 = 0.25f, .y2 = 1.0f};
            case TimingFunction::EaseIn: return {.x1 = 0.42f, .y1 = 0.0f, .x2 = 1.0f, .y2 = 1.0f};
            case TimingFunction::EaseOut: return {.x1 = 0.0f, .y1 = 0.0f, .x2 = 0.58f, .y2 = 1.0f};
            case TimingFunction::EaseInOut: return {.x1 = 0.42f, .y1 = 0.0f, .x2 = 0.58f, .y2 = 1.0f};
        }
        throw std::invalid_argument(std::format("unknown timing function {}", static_cast<std::int32_t>(timing)));
    }

    // One coordinate of a Bézier from 0 to 1 through controls a and b, at parameter t.
    float CubicAt(float a, float b, float t) {
        const float rest = 1.0f - t;
        return (3.0f * rest * rest * t * a) + (3.0f * rest * t * t * b) + (t * t * t);
    }

} // namespace

// x rises with t for every CSS keyword, so halving finds the t whose x is progress.
float Eased(TimingFunction timing, float progress) {
    const Bezier curve = CurveOf(timing);
    float        low   = 0.0f;
    float        high  = 1.0f;
    for (std::int32_t step = 0; step < 32; ++step) {
        const float middle = (low + high) / 2.0f;
        if (CubicAt(curve.x1, curve.x2, middle) < progress) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return CubicAt(curve.y1, curve.y2, (low + high) / 2.0f);
}

AnimatedValue::AnimatedValue(float value) : _from(value), _to(value) {}

void AnimatedValue::Retarget(float target, const Transition& transition, std::chrono::steady_clock::time_point now) {
    const float current = Value(now);
    const float share   = std::abs(target - current);
    _from               = current;
    _to                 = target;
    _duration           = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(static_cast<float>(transition.duration.count()) * share)));
    _timing             = transition.timing;
    _start              = now;
}

float AnimatedValue::Value(std::chrono::steady_clock::time_point now) const {
    if (!Running(now)) {
        return _to;
    }
    const float progress = std::chrono::duration<float>(now - _start) / std::chrono::duration<float>(_duration);
    return _from + ((_to - _from) * Eased(_timing, progress));
}

bool AnimatedValue::Running(std::chrono::steady_clock::time_point now) const {
    return _duration.count() > 0 && now - _start < _duration;
}

ColorFade::ColorFade(const D2D1_COLOR_F& color) : _from(color), _to(color) {}

void ColorFade::Retarget(const D2D1_COLOR_F& target, const Transition& transition, std::chrono::steady_clock::time_point now) {
    if (target.r == _to.r && target.g == _to.g && target.b == _to.b && target.a == _to.a) {
        return;
    }
    _from     = Color(now);
    _to       = target;
    _progress = AnimatedValue(0.0f);
    _progress.Retarget(1.0f, transition, now);
}

D2D1_COLOR_F ColorFade::Color(std::chrono::steady_clock::time_point now) const {
    return MixColors(_from, _to, _progress.Value(now));
}

bool ColorFade::Running(std::chrono::steady_clock::time_point now) const {
    return _progress.Running(now);
}

D2D1_COLOR_F MixColors(const D2D1_COLOR_F& first, const D2D1_COLOR_F& second, float share) {
    const float alpha = first.a + ((second.a - first.a) * share);
    if (alpha <= 0.0f) {
        return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto channel = [&](float from, float to) { return ((from * first.a) + (((to * second.a) - (from * first.a)) * share)) / alpha; };
    return D2D1::ColorF(channel(first.r, second.r), channel(first.g, second.g), channel(first.b, second.b), alpha);
}
