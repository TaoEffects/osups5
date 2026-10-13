// SPDX-License-Identifier: GPL-3.0-or-later
// Courbes d'animation et lissages indépendants de la fréquence d'image.
#pragma once

#include <cmath>
#include <cstdint>

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

// Courbes nommées comme dans osu-framework (In = InQuad, Out = OutQuad).
enum class Ease : uint8_t {
    None, In, Out, InOut, InCubic, OutCubic, InOutCubic, InQuart, OutQuart, InOutQuart, InQuint, OutQuint, InOutQuint,
    InSine, OutSine, InOutSine, InExpo, OutExpo, InOutExpo, OutElastic, OutElasticHalf, OutElasticQuarter, OutBack,
    OutBounce, OutPow10, InOutExpoSoft
};

inline float ease(Ease e, float t) {
    t = clamp01(t);
    switch (e) {
    case Ease::None: return t;
    case Ease::In: return t * t;
    case Ease::Out: return t * (2 - t);
    case Ease::InOut: return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
    case Ease::InCubic: return t * t * t;
    case Ease::OutCubic: return outCubic(t);
    case Ease::InOutCubic: return inOutCubic(t);
    case Ease::InQuart: return t * t * t * t;
    case Ease::OutQuart: { const float u = 1 - t; return 1 - u * u * u * u; }
    case Ease::InOutQuart: return t < 0.5f ? 8 * t * t * t * t : 1 - std::pow(-2 * t + 2, 4.0f) / 2;
    case Ease::InQuint: return t * t * t * t * t;
    case Ease::OutQuint: return outQuint(t);
    case Ease::InOutQuint: return t < 0.5f ? 16 * t * t * t * t * t : 1 - std::pow(-2 * t + 2, 5.0f) / 2;
    case Ease::InSine: return 1 - std::cos(t * kPi / 2);
    case Ease::OutSine: return std::sin(t * kPi / 2);
    case Ease::InOutSine: return -(std::cos(kPi * t) - 1) / 2;
    case Ease::InExpo: return t == 0 ? 0 : std::pow(2.0f, 10 * t - 10);
    case Ease::OutExpo: return t == 1 ? 1 : 1 - std::pow(2.0f, -10 * t);
    case Ease::InOutExpo:
    case Ease::InOutExpoSoft:
        if (t == 0 || t == 1) return t;
        return t < 0.5f ? std::pow(2.0f, 20 * t - 10) / 2 : (2 - std::pow(2.0f, -20 * t + 10)) / 2;
    case Ease::OutElastic: return outElastic(t);
    case Ease::OutElasticHalf:
        if (t == 0 || t == 1) return t;
        return std::pow(2.0f, -10 * t) * std::sin((0.5f * t - 0.075f) * (2 * kPi) / 0.3f) + 1;
    case Ease::OutElasticQuarter:
        if (t == 0 || t == 1) return t;
        return std::pow(2.0f, -10 * t) * std::sin((0.25f * t - 0.075f) * (2 * kPi) / 0.3f) + 1;
    case Ease::OutBack: return outBack(t);
    case Ease::OutBounce: {
        const float n = 7.5625f, d = 2.75f;
        if (t < 1 / d) return n * t * t;
        if (t < 2 / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
        if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
        t -= 2.625f / d;
        return n * t * t + 0.984375f;
    }
    case Ease::OutPow10: { const float u = t - 1; return 1 - std::pow(std::fabs(u), 10.0f); }
    }
    return t;
}

// Valeur animée façon "transform" de lazer : part de sa valeur actuelle vers une
// cible, en `duration` ms après `delay` ms, selon une courbe. Temps en ms.
struct Tween {
    float from = 0, to = 0;
    double start = 0, duration = 0;
    Ease easing = Ease::None;

    Tween() = default;
    Tween(float v) : from(v), to(v) {}  // NOLINT(google-explicit-constructor)
    float at(double nowMs) const {
        if (duration <= 0 || nowMs >= start + duration) return nowMs >= start ? to : from;
        if (nowMs <= start) return from;
        return from + (to - from) * ease(easing, static_cast<float>((nowMs - start) / duration));
    }
    // Nouvelle cible : on repart de la valeur au moment où l'animation commence.
    void go(double nowMs, float target, double durationMs, Ease e = Ease::OutQuint, double delayMs = 0) {
        from = at(nowMs + delayMs);
        to = target;
        start = nowMs + delayMs;
        duration = durationMs;
        easing = e;
    }
    // Démarre depuis une valeur imposée.
    void fromTo(double nowMs, float a, float b, double durationMs, Ease e = Ease::OutQuint, double delayMs = 0) {
        from = a;
        to = b;
        start = nowMs + delayMs;
        duration = durationMs;
        easing = e;
    }
    void snap(float v) {
        from = to = v;
        duration = 0;
        start = 0;
    }
    bool done(double nowMs) const { return nowMs >= start + duration; }
    float target() const { return to; }
};

// Rapprochement continu de lazer (Interpolation.DampContinuously) : demi-vie en ms.
inline float damp(float current, float target, float halfLifeMs, float dtMs) {
    if (halfLifeMs <= 0) return target;
    return target + (current - target) * std::exp2(-dtMs / halfLifeMs);
}

}  // namespace anim
