// SPDX-License-Identifier: GPL-3.0-or-later
// Courbes d'animation et lissages indépendants de la fréquence d'image.
#pragma once

#include <cmath>

namespace anim {

constexpr float kPi = 3.14159265358979f;

inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float outCubic(float t) {
    t = clamp01(t) - 1;
    return t * t * t + 1;
}
inline float outQuint(float t) {
    t = clamp01(t) - 1;
    return t * t * t * t * t + 1;
}
inline float inOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}
inline float outBack(float t) {
    t = clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
}
inline float outElastic(float t) {
    t = clamp01(t);
    if (t == 0 || t == 1) return t;
    return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * (2 * kPi / 3)) + 1;
}
// Rapproche `current` de `target` de façon exponentielle (demi-vie en secondes).
inline float approach(float current, float target, float dt, float halfLife) {
    if (halfLife <= 0) return target;
    return target + (current - target) * std::pow(0.5f, dt / halfLife);
}

}  // namespace anim
