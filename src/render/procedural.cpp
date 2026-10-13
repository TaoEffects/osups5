// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/procedural.h"

#include <cmath>
#include <vector>

namespace gfx {
namespace {

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Remplit une texture carrée à partir d'une fonction (distance normalisée -> gris, alpha).
template <typename F>
Texture radial(int size, F shade) {
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4);
    const float half = static_cast<float>(size) * 0.5f;
    const float aa = 1.5f / half;  // largeur de l'anticrénelage
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - half) / half;
            const float dy = (static_cast<float>(y) + 0.5f - half) / half;
            const float d = std::sqrt(dx * dx + dy * dy);
            float grey = 1, alpha = 0;
            shade(d, aa, grey, alpha);
            uint8_t *p = &px[(static_cast<size_t>(y) * size + x) * 4];
            const uint8_t g = static_cast<uint8_t>(clamp01(grey) * 255.0f);
            p[0] = p[1] = p[2] = g;
            p[3] = static_cast<uint8_t>(clamp01(alpha) * 255.0f);
        }
    }
    return createTexture(size, size, px.data(), true);
}

}  // namespace

Texture makeHitCircle(int size) {
    return radial(size, [](float d, float aa, float &grey, float &alpha) {
        const float outer = clamp01((0.98f - d) / aa);       // bord extérieur
        const float border = clamp01((d - 0.84f) / aa);      // anneau blanc
        alpha = outer;
        grey = 0.55f + 0.45f * border + 0.15f * (1.0f - d);  // centre teinté, bord blanc
    });
}

Texture makeRing(int size, float thicknessRatio) {
    return radial(size, [thicknessRatio](float d, float aa, float &grey, float &alpha) {
        const float outer = clamp01((0.98f - d) / aa);
        const float inner = clamp01((d - (0.98f - thicknessRatio)) / aa);
        alpha = outer * inner;
        grey = 1;
    });
}

Texture makeSoftDot(int size) {
    return radial(size, [](float d, float aa, float &grey, float &alpha) {
        const float core = clamp01((0.45f - d) / aa);
        const float glow = clamp01(1.0f - d) * 0.5f;
        alpha = core > glow ? core : glow;
        grey = 1;
    });
}

}  // namespace gfx
