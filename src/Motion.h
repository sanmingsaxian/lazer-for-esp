#pragma once

#include <Arduino.h>
#include <math.h>

// Closed-form mass-spring-damper motion. Retargeting preserves the presented
// value and velocity, matching Lazer's interruptible Apple-style transitions.
class Spring {
public:
    float value = 0;
    float velocity = 0;
    float target = 0;

    void snap(float value) {
        this->value = target = value;
        velocity = 0;
        _from = value;
        _startMs = millis();
        _moving = false;
    }

    void retarget(float next, float duration = 0.35f, float bounce = 0.0f, bool reduce = false) {
        update(millis());
        target = next;
        if (reduce) {
            snap(next);
            return;
        }
        _from = value;
        _initialVelocity = velocity;
        _duration = duration > 0.05f ? duration : 0.35f;
        _bounce = constrain(bounce, 0.0f, 0.8f);
        _startMs = millis();
        _moving = true;
    }

    void update(uint32_t now) {
        if (!_moving) return;
        float t = (now - _startMs) / 1000.0f;
        const float omega = 2.0f * PI / _duration;
        const float zeta = 1.0f - _bounce;
        const float x0 = _from - target;
        const float v0 = _initialVelocity;
        float x, v;
        if (fabsf(zeta - 1.0f) < 0.001f) {
            const float a = x0;
            const float b = v0 + omega * x0;
            const float e = expf(-omega * t);
            x = (a + b * t) * e;
            v = (b - omega * (a + b * t)) * e;
        } else if (zeta < 1.0f) {
            const float wd = omega * sqrtf(1.0f - zeta * zeta);
            const float e = expf(-zeta * omega * t);
            const float c = cosf(wd * t);
            const float s = sinf(wd * t);
            const float b = (v0 + zeta * omega * x0) / wd;
            x = e * (x0 * c + b * s);
            v = e * ((-zeta * omega) * (x0 * c + b * s) + wd * (-x0 * s + b * c));
        } else {
            // This port only uses critically damped or lightly bouncing springs.
            const float a = x0;
            const float b = v0 + omega * x0;
            const float e = expf(-omega * t);
            x = (a + b * t) * e;
            v = (b - omega * (a + b * t)) * e;
        }
        value = target + x;
        velocity = v;
        if ((fabsf(x) < 0.0005f && fabsf(v) < 0.005f) || t > _duration * 4.0f) {
            snap(target);
        }
    }

    bool moving() const { return _moving; }

private:
    float _from = 0;
    float _initialVelocity = 0;
    float _duration = 0.35f;
    float _bounce = 0;
    uint32_t _startMs = 0;
    bool _moving = false;
};

inline float easeOutCubic(float t) {
    t = constrain(t, 0.0f, 1.0f);
    return 1.0f - powf(1.0f - t, 3.0f);
}

inline float easeInOutCubic(float t) {
    t = constrain(t, 0.0f, 1.0f);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
