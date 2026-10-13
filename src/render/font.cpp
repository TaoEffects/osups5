// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/font.h"

#include <vector>

// Tableaux instanciés par third_party/font8x8/font8x8.c (compilé en C).
extern "C" {
extern char font8x8_basic[128][8];
extern char font8x8_control[32][8];
extern char font8x8_ext_latin[96][8];
}

namespace gfx {
namespace {

// Décode un point de code UTF-8 ; renvoie '?' pour ce qui sort du Latin-1.
unsigned nextCodepoint(const char *&s) {
    const unsigned char c = static_cast<unsigned char>(*s++);
    if (c < 0x80) return c;
    unsigned cp = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        extra = 3;
    } else {
        return '?';
    }
    for (int i = 0; i < extra; ++i) {
        const unsigned char cc = static_cast<unsigned char>(*s);
        if ((cc & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (cc & 0x3F);
        ++s;
    }
    return cp <= 0xFF ? cp : '?';
}

const char *glyphRows(unsigned cp) {
    if (cp < 0x80) return font8x8_basic[cp];
    if (cp < 0xA0) return font8x8_control[cp - 0x80];
    return font8x8_ext_latin[cp - 0xA0];
}

}  // namespace

bool BitmapFont::init() {
    // Atlas 16x16 glyphes de 8x8 = 128x128, blanc + alpha, filtrage au plus proche.
    std::vector<uint8_t> pixels(128 * 128 * 4, 0);
    for (unsigned cp = 0; cp < 256; ++cp) {
        const char *rows = glyphRows(cp);
        const int gx = static_cast<int>(cp % 16) * 8;
        const int gy = static_cast<int>(cp / 16) * 8;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (static_cast<unsigned char>(rows[y]) & (1u << x)) {
                    uint8_t *p = &pixels[((gy + y) * 128 + gx + x) * 4];
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
            }
        }
    }
    atlas_ = createTexture(128, 128, pixels.data(), false);
    return atlas_.id != 0;
}

void BitmapFont::shutdown() { destroyTexture(atlas_); }

float BitmapFont::draw(SpriteBatch &batch, float x, float y, float scale, uint32_t color, const char *s) const {
    const float size = 8.0f * scale;
    const float startX = x;
    while (*s) {
        const unsigned cp = nextCodepoint(s);
        if (cp == '\n') {
            x = startX;
            y += size * 1.25f;
            continue;
        }
        if (cp != ' ') {
            const float u0 = static_cast<float>(cp % 16) / 16.0f;
            const float v0 = static_cast<float>(cp / 16) / 16.0f;
            batch.quad(atlas_, x, y, size, size, color, u0, v0, u0 + 1.0f / 16.0f, v0 + 1.0f / 16.0f);
        }
        x += size;
    }
    return x - startX;
}

float BitmapFont::drawShadow(SpriteBatch &batch, float x, float y, float scale, uint32_t color, const char *s) const {
    draw(batch, x + scale, y + scale, scale, rgba(0, 0, 0, static_cast<uint8_t>(color >> 24)), s);
    return draw(batch, x, y, scale, color, s);
}

float BitmapFont::width(float scale, const char *s) {
    float w = 0, best = 0;
    while (*s) {
        const unsigned cp = nextCodepoint(s);
        if (cp == '\n') {
            best = w > best ? w : best;
            w = 0;
            continue;
        }
        w += 8.0f * scale;
    }
    return w > best ? w : best;
}

}  // namespace gfx
