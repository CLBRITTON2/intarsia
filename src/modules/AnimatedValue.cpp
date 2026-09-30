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
