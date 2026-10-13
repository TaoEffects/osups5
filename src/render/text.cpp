// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

namespace {

constexpr int kPad = 8;                   // marge du champ de distance (pixels de l'atlas)
constexpr unsigned char kOnEdge = 128;    // valeur sur le contour
constexpr float kDistScale = 16.0f;       // 1 pixel = 16 / 255 du champ

struct FontFile {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    float scale = 1;
    bool ok = false;
    bool open(const std::string &path, float px) {
        if (path.empty() || !fsu::readFile(path, data, 8u << 20)) return false;
        ok = stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)) != 0;
        if (ok) scale = stbtt_ScaleForPixelHeight(&info, px);
        return ok;
    }
};

}  // namespace

bool Font::load(const std::string &path, const std::string &fallbackPath, bool allGlyphs) {
    FontFile main, fb;
    if (!main.open(path, bakePx_)) {
        LOG("text: police introuvable: %s", path.c_str());
        return false;
    }
    fb.open(fallbackPath, bakePx_);
    scale_ = main.scale;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&main.info, &asc, &desc, &gap);
    ascent_ = static_cast<float>(asc) * scale_;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (stbtt_GetCodepointBox(&main.info, 'H', &x0, &y0, &x1, &y1)) capHeight_ = static_cast<float>(y1) * scale_ / bakePx_;

    struct Range {
        unsigned first, last;
    };
    const Range ranges[] = {{32, 126},       {160, 255},       {0x152, 0x153},   {0x2010, 0x2027}, {0x2030, 0x203A},
                            {0x2122, 0x2122}, {0x2190, 0x2193}, {0x2605, 0x2606}, {0x266A, 0x266B}, {0x2713, 0x2715},
                            {0x221E, 0x221E}, {0x2264, 0x2265}};
    struct Bitmap {
        unsigned cp;
        Glyph g;
        int w, h;
        unsigned char *pixels;
    };
    std::vector<Bitmap> bitmaps;
    // Police d'icônes : tous ses glyphes, quels que soient leurs points de code.
    std::vector<Range> list(std::begin(ranges), std::end(ranges));
    if (allGlyphs) {
        list.clear();
        for (unsigned cp = 32; cp < 0xFFFF; ++cp)
            if (stbtt_FindGlyphIndex(&main.info, static_cast<int>(cp)) != 0) list.push_back({cp, cp});
    }
    for (const Range &r : list) {
        for (unsigned cp = r.first; cp <= r.last; ++cp) {
            FontFile *f = &main;
            int index = stbtt_FindGlyphIndex(&main.info, static_cast<int>(cp));
            if (index == 0 && cp != 32) {
                if (!fb.ok) continue;
                index = stbtt_FindGlyphIndex(&fb.info, static_cast<int>(cp));
                if (index == 0) continue;
                f = &fb;
            }
            Bitmap b{};
            b.cp = cp;
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&f->info, index, &adv, &lsb);
            b.g.advance = static_cast<float>(adv) * f->scale;
            b.g.index = index;
            b.g.fallback = f == &fb;
            int xoff = 0, yoff = 0;
            b.pixels = stbtt_GetGlyphSDF(&f->info, f->scale, index, kPad, kOnEdge, kDistScale, &b.w, &b.h, &xoff, &yoff);
            b.g.x0 = static_cast<float>(xoff);
            b.g.y0 = static_cast<float>(yoff) + ascent_;
            b.g.x1 = b.g.x0 + static_cast<float>(b.w);
            b.g.y1 = b.g.y0 + static_cast<float>(b.h);
            bitmaps.push_back(b);
        }
    }
    // Rangement en étagères, des plus hauts aux plus petits.
    std::vector<Bitmap *> order;
    for (Bitmap &b : bitmaps) order.push_back(&b);
    std::sort(order.begin(), order.end(), [](const Bitmap *a, const Bitmap *b) { return a->h > b->h; });
    const int atlasW = 1024;
    int x = 1, y = 1, shelf = 0;
    for (Bitmap *b : order) {
        if (!b->pixels) continue;
        if (x + b->w + 1 > atlasW) {
            x = 1;
            y += shelf + 1;
            shelf = 0;
        }
        b->g.u0 = static_cast<float>(x);
        b->g.v0 = static_cast<float>(y);
        x += b->w + 1;
        shelf = std::max(shelf, b->h);
    }
    int atlasH = 64;
    while (atlasH < y + shelf + 1) atlasH *= 2;
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasW) * static_cast<size_t>(atlasH), 0);
    for (Bitmap &b : bitmaps) {
        if (b.pixels) {
            const int px = static_cast<int>(b.g.u0), py = static_cast<int>(b.g.v0);
            for (int row = 0; row < b.h; ++row)
                memcpy(&pixels[static_cast<size_t>(py + row) * atlasW + static_cast<size_t>(px)], b.pixels + row * b.w,
                       static_cast<size_t>(b.w));
            b.g.u0 = static_cast<float>(px) / atlasW;
            b.g.v0 = static_cast<float>(py) / static_cast<float>(atlasH);
            b.g.u1 = static_cast<float>(px + b.w) / atlasW;
            b.g.v1 = static_cast<float>(py + b.h) / static_cast<float>(atlasH);
            stbtt_FreeSDF(b.pixels, nullptr);
        } else {
            b.g.x1 = b.g.x0;  // espace : rien à dessiner
        }
        glyphs_[b.cp] = b.g;
    }
    atlas_ = createAlphaTexture(atlasW, atlasH, pixels.data(), true);
    ttf_ = std::move(main.data);
    info_.resize(sizeof(stbtt_fontinfo));
    stbtt_fontinfo reinit{};
    stbtt_InitFont(&reinit, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0));
    memcpy(info_.data(), &reinit, sizeof(reinit));
    LOG("text: %s : %zu glyphes, atlas SDF %dx%d", path.c_str(), glyphs_.size(), atlasW, atlasH);
    return true;
}

void Font::shutdown() {
    destroyTexture(atlas_);
    glyphs_.clear();
    kernCache_.clear();
}

const Font::Glyph *Font::find(unsigned cp) const {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    it = glyphs_.find('?');
    return it != glyphs_.end() ? &it->second : nullptr;
}

float Font::kern(const Glyph *a, const Glyph *b) const {
    if (!a || !b || a->fallback || b->fallback || info_.empty()) return 0;
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(a->index)) << 32) | static_cast<uint32_t>(b->index);
    auto it = kernCache_.find(key);
    if (it != kernCache_.end()) return it->second;
    const auto *info = reinterpret_cast<const stbtt_fontinfo *>(info_.data());
    const float k = static_cast<float>(stbtt_GetGlyphKernAdvance(info, a->index, b->index)) * scale_;
    kernCache_[key] = k;
    return k;
}

float Font::measure(float size, const char *s, float spacing) const {
    const float k = size / bakePx_;
    float w = 0;
    const Glyph *prev = nullptr;
    int count = 0;
    while (*s) {
        const unsigned cp = decodeUtf8(s);
        const Glyph *g = find(cp);
        if (!g) continue;
        w += (g->advance + kern(prev, g)) * k;
        prev = g;
        ++count;
    }
    return w + spacing * static_cast<float>(count > 0 ? count - 1 : 0);
}

float Font::drawSdf(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *s, Align align,
                    float spacing, float dilate, float soft) const {
    if (!loaded() || !s || (color >> 24) == 0) return 0;
    const float width = measure(size, s, spacing);
    if (align == Align::Center) x -= width * 0.5f;
    if (align == Align::Right) x -= width;
    const float k = size / bakePx_;
    float pen = x;
    const Glyph *prev = nullptr;
    while (*s) {
        const unsigned cp = decodeUtf8(s);
        const Glyph *g = find(cp);
        if (!g) continue;
        pen += kern(prev, g) * k;
        if (g->x1 > g->x0)
            batch.sdfQuad(atlas_, pen + g->x0 * k, y + g->y0 * k, (g->x1 - g->x0) * k, (g->y1 - g->y0) * k, color, g->u0,
                          g->v0, g->u1, g->v1, dilate, soft);
        pen += g->advance * k + spacing;
        prev = g;
    }
    return width;
}

void Font::drawGlyphCentered(SpriteBatch &batch, const char *s, float cx, float cy, float size, uint32_t color) const {
    if (!loaded() || !s || (color >> 24) == 0) return;
    const unsigned cp = decodeUtf8(s);
    const Glyph *g = find(cp);
    if (!g || g->x1 <= g->x0) return;
    const float k = size / bakePx_;
    const float w = (g->x1 - g->x0) * k, h = (g->y1 - g->y0) * k;
    batch.sdfQuad(atlas_, cx - w * 0.5f, cy - h * 0.5f, w, h, color, g->u0, g->v0, g->u1, g->v1, 0, 0);
}

float Font::draw(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *s, Align align,
                 float spacing) const {
    return drawSdf(batch, x, y, size, color, s, align, spacing, 0, 0);
}

float Font::drawShadow(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *s, Align align,
                       float spacing) const {
    const float a = static_cast<float>(color >> 24) / 255.0f;
    drawSdf(batch, x, y + std::max(1.5f, size * 0.05f), size, rgba(0, 0, 0, static_cast<uint8_t>(90 * a)), s, align,
            spacing, 0.04f, 0.12f);
    return draw(batch, x, y, size, color, s, align, spacing);
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
