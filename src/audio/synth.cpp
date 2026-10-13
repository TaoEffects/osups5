// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/synth.h"

#include <cmath>
#include <cstdint>

namespace audio {
namespace {

constexpr float kTwoPi = 6.28318530718f;

float noise(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFF) / 32768.0f - 1.0f;
}

// Génère `ms` millisecondes avec une fonction f(t) -> échantillon mono.
template <typename F>
Sample render(int ms, F f) {
    Sample s;
    const int frames = Mixer::kRate * ms / 1000;
    s.stereo.resize(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / Mixer::kRate;
        float v = f(t);
        // Fondu de sortie de 3 ms contre les clics.
        const float tail = static_cast<float>(frames - i) / (Mixer::kRate * 0.003f);
        if (tail < 1) v *= tail;
        s.stereo[i * 2] = s.stereo[i * 2 + 1] = v;
    }
    return s;
}

// Simple passe-bas d'un pôle appliqué à un bruit.
struct Lowpass {
    float y = 0, a;
    explicit Lowpass(float cutoff) : a(1.0f - std::exp(-kTwoPi * cutoff / Mixer::kRate)) {}
    float operator()(float x) { return y += a * (x - y); }
};

}  // namespace

Sample makeHitSound() {
    uint32_t rng = 0x12345678u;
    return render(70, [&](float t) {
        return 0.55f * std::sin(kTwoPi * 520.0f * t) * std::exp(-t * 55.0f) + 0.35f * noise(rng) * std::exp(-t * 400.0f);
    });
}

Sample makeTick(float hz) {
    return render(30, [hz](float t) { return 0.4f * std::sin(kTwoPi * hz * t) * std::exp(-t * 150.0f); });
}

void SoundBank::generate() {
    // Trois timbres pour les trois jeux de sons d'osu! (normal, soft, drum).
    const float body[3] = {620.0f, 440.0f, 160.0f};
    const float decay[3] = {60.0f, 45.0f, 30.0f};
    const float clickAmt[3] = {0.45f, 0.18f, 0.25f};
    for (int set = 0; set < 3; ++set) {
        uint32_t rng = 0xBEEF0000u + static_cast<uint32_t>(set);
        Lowpass lp(set == 1 ? 3000.0f : 7000.0f);
        hit[set][0] = render(90, [&](float t) {
            const float pitch = body[set] * (set == 2 ? (1.0f + 1.5f * std::exp(-t * 60.0f)) : 1.0f);
            return 0.5f * std::sin(kTwoPi * pitch * t) * std::exp(-t * decay[set]) +
                   clickAmt[set] * lp(noise(rng)) * std::exp(-t * 300.0f);
        });
        hit[set][1] = render(220, [&](float t) {  // whistle : note aiguë filée
            const float f = (set == 2 ? 1300.0f : 1800.0f) * (1.0f + 0.02f * std::sin(kTwoPi * 6 * t));
            return 0.22f * std::sin(kTwoPi * f * t) * std::exp(-t * 12.0f) * (1 - std::exp(-t * 300.0f));
        });
        uint32_t rng2 = 0xCAFE0000u + static_cast<uint32_t>(set);
        Lowpass lp2(set == 1 ? 5000.0f : 9000.0f);
        hit[set][2] = render(700, [&](float t) {  // finish : cymbale
            return 0.32f * lp2(noise(rng2)) * std::exp(-t * 5.0f) + 0.12f * std::sin(kTwoPi * 330.0f * t) * std::exp(-t * 9.0f);
        });
        uint32_t rng3 = 0xC1A90000u + static_cast<uint32_t>(set);
        Lowpass lp3(2500.0f);
        hit[set][3] = render(120, [&](float t) {  // clap : bruit en double impulsion
            const float env = std::exp(-t * 45.0f) + 0.6f * (t > 0.012f ? std::exp(-(t - 0.012f) * 60.0f) : 0.0f);
            return 0.45f * (noise(rng3) - lp3(noise(rng3))) * env;
        });
    }
    sliderTick = render(40, [](float t) { return 0.18f * std::sin(kTwoPi * 2400.0f * t) * std::exp(-t * 120.0f); });
    sliderSlide = render(30, [](float) { return 0.0f; });
    spinnerBonus = render(160, [](float t) { return 0.25f * std::sin(kTwoPi * (1500.0f + 2000.0f * t) * t) * std::exp(-t * 18.0f); });
    comboBreak = render(350, [](float t) {
        const float f = 300.0f * std::exp(-t * 3.0f);
        return 0.35f * (std::sin(kTwoPi * f * t) > 0 ? 1.0f : -1.0f) * std::exp(-t * 8.0f) * 0.5f;
    });
    uiHover = render(35, [](float t) { return 0.12f * std::sin(kTwoPi * 1800.0f * t) * std::exp(-t * 140.0f); });
    uiSelect = render(140, [](float t) {
        const float f = t < 0.05f ? 880.0f : 1320.0f;
        return 0.22f * std::sin(kTwoPi * f * t) * std::exp(-t * 22.0f);
    });
    uiBack = render(120, [](float t) {
        const float f = t < 0.05f ? 990.0f : 660.0f;
        return 0.2f * std::sin(kTwoPi * f * t) * std::exp(-t * 25.0f);
    });
    uint32_t rngw = 0x5EED5EEDu;
    Lowpass lpw(1200.0f);
    uiOpen = render(260, [&](float t) {  // souffle (transition d'écran)
        const float env = std::sin(3.14159265f * (t / 0.26f < 1.0f ? t / 0.26f : 1.0f));
        return 0.3f * lpw(noise(rngw)) * env;
    });
    uiError = render(200, [](float t) { return 0.2f * std::sin(kTwoPi * 220.0f * t) * std::exp(-t * 12.0f); });
    uiDownloaded = render(400, [](float t) {
        const float f = t < 0.1f ? 784.0f : (t < 0.2f ? 988.0f : 1175.0f);
        return 0.2f * std::sin(kTwoPi * f * t) * std::exp(-t * 6.0f);
    });
}

}  // namespace audio
