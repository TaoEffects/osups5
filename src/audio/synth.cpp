// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/synth.h"

#include <cmath>
#include <cstdint>

namespace audio {
namespace {

constexpr float kTwoPi = 6.28318530718f;

// Bruit blanc déterministe (xorshift), suffisant pour une attaque percussive.
float noise(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFF) / 32768.0f - 1.0f;
}

}  // namespace

Sample makeHitSound() {
    Sample s;
    const int frames = Mixer::kRate * 70 / 1000;
    s.stereo.resize(static_cast<size_t>(frames) * 2);
    uint32_t rng = 0x12345678u;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / Mixer::kRate;
        const float body = std::sin(kTwoPi * 520.0f * t) * std::exp(-t * 55.0f);
        const float click = noise(rng) * std::exp(-t * 400.0f);
        const float v = 0.55f * body + 0.35f * click;
        s.stereo[i * 2] = v;
        s.stereo[i * 2 + 1] = v;
    }
    return s;
}

Sample makeTick(float hz) {
    Sample s;
    const int frames = Mixer::kRate * 30 / 1000;
    s.stereo.resize(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / Mixer::kRate;
        const float v = 0.4f * std::sin(kTwoPi * hz * t) * std::exp(-t * 150.0f);
        s.stereo[i * 2] = v;
        s.stereo[i * 2 + 1] = v;
    }
    return s;
}

}  // namespace audio
