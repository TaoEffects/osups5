// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/text.h"

#include <cmath>
#include <vector>

#include "core/fsutil.h"
#include "core/log.h"
#include "stb/stb_truetype.h"

namespace gfx {

unsigned decodeUtf8(const char *&s) {
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
        return 0xFFFD;
    }
    for (int i = 0; i < extra; ++i) {
        const unsigned char cc = static_cast<unsigned char>(*s);
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++s;
    }
    return cp;
}

bool Font::load(const std::string &path, float bakePx, int atlasW, int atlasH) {
    std::vector<uint8_t> ttf;
    if (!fsu::readFile(path, ttf, 8u << 20)) {
        LOG("text: police introuvable: %s", path.c_str());
        return false;
    }
    bakePx_ = bakePx;
    // Latin-1 complet + ponctuation typographique, flèches et quelques symboles.
    struct Range {
        int first, count;
    };
    const Range ranges[] = {{32, 95}, {160, 96}, {0x2010, 0x18}, {0x2190, 4}, {0x2022, 1}, {0x2605, 1}, {0x266A, 1},
                            {0x00D7, 1}, {0x0152, 2}, {0x2122, 1}, {0x2713, 1}, {0x2715, 1}};
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasW) * atlasH);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, pixels.data(), atlasW, atlasH, 0, 1, nullptr)) return false;
    stbtt_PackSetOversampling(&pc, 2, 2);
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0));
    const float scale = stbtt_ScaleForPixelHeight(&info, bakePx);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    ascent_ = static_cast<float>(asc) * scale;
    int packed = 0;
    for (const Range &r : ranges) {
        std::vector<stbtt_packedchar> chars(static_cast<size_t>(r.count));
        if (!stbtt_PackFontRange(&pc, ttf.data(), 0, bakePx, r.first, r.count, chars.data())) {
            LOG("text: atlas %dx%d trop petit pour %s @%.0f", atlasW, atlasH, path.c_str(), bakePx);
            continue;
        }
        for (int i = 0; i < r.count; ++i) {
            const unsigned cp = static_cast<unsigned>(r.first + i);
            if (cp != 32 && stbtt_FindGlyphIndex(&info, static_cast<int>(cp)) == 0) continue;
            const stbtt_packedchar &pch = chars[static_cast<size_t>(i)];
            Glyph g;
            g.x0 = pch.xoff;
            g.y0 = pch.yoff + ascent_;
            g.x1 = pch.xoff2;
            g.y1 = pch.yoff2 + ascent_;
            g.u0 = static_cast<float>(pch.x0) / static_cast<float>(atlasW);
            g.v0 = static_cast<float>(pch.y0) / static_cast<float>(atlasH);
            g.u1 = static_cast<float>(pch.x1) / static_cast<float>(atlasW);
            g.v1 = static_cast<float>(pch.y1) / static_cast<float>(atlasH);
            g.advance = pch.xadvance;
            glyphs_[cp] = g;
            ++packed;
        }
    }
    stbtt_PackEnd(&pc);
    atlas_ = createAlphaTexture(atlasW, atlasH, pixels.data(), true);
    LOG("text: %s @%.0fpx, %d glyphes, atlas %dx%d", path.c_str(), bakePx, packed, atlasW, atlasH);
    return true;
}

void Font::shutdown() {
    destroyTexture(atlas_);
    glyphs_.clear();
}

const Font::Glyph *Font::find(unsigned cp) const {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    it = glyphs_.find('?');
    return it != glyphs_.end() ? &it->second : nullptr;
}

float Font::measure(float size, const char *s) const {
    const float k = size / bakePx_;
    float w = 0;
    while (*s) {
        const unsigned cp = decodeUtf8(s);
        if (const Glyph *g = find(cp)) w += g->advance * k;
    }
    return w;
}

float Font::draw(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *s, Align align) const {
    if (!loaded() || !s) return 0;
    const float width = measure(size, s);
    if (align == Align::Center) x -= width * 0.5f;
    if (align == Align::Right) x -= width;
    const float k = size / bakePx_;
    float pen = x;
    while (*s) {
        const unsigned cp = decodeUtf8(s);
        const Glyph *g = find(cp);
        if (!g) continue;
        if (g->x1 > g->x0) {
            // Position arrondie au pixel virtuel pour un texte net.
            const float gx = std::round(pen + g->x0 * k);
            const float gy = std::round(y + g->y0 * k);
            batch.quad(atlas_, gx, gy, (g->x1 - g->x0) * k, (g->y1 - g->y0) * k, color, g->u0, g->v0, g->u1, g->v1);
        }
        pen += g->advance * k;
    }
    return width;
}

float Font::drawShadow(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *s,
                       Align align) const {
    const float off = size > 30 ? 2.0f : 1.5f;
    draw(batch, x + off, y + off, size, rgba(0, 0, 0, static_cast<uint8_t>((color >> 24) * 0.6f)), s, align);
    return draw(batch, x, y, size, color, s, align);
}

std::string Font::ellipsize(float size, const std::string &text, float maxWidth) const {
    if (measure(size, text.c_str()) <= maxWidth) return text;
    std::string t = text;
    while (!t.empty()) {
        // Retire un point de code UTF-8 complet.
        size_t cut = t.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;
        t.resize(cut);
        const std::string candidate = t + "…";
        if (measure(size, candidate.c_str()) <= maxWidth) return candidate;
    }
    return "…";
}

}  // namespace gfx
