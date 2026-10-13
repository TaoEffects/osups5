// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/text.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/fsutil.h"
#include "core/i18n.h"
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


// Champ de distance calculé depuis un rendu suréchantillonné (x4) du glyphe.
// stbtt_GetGlyphSDF mesure la distance à chaque contour séparément : quand une
// police dessine une lettre en formes qui se chevauchent (barre du « t »
// d'Outfit Bold), des coutures apparaissent à l'intérieur. Le rendu bitmap, lui,
// fusionne les formes (règle non nulle) ; la distance euclidienne exacte
// (Felzenszwalb) donne ensuite un champ propre. Même convention que stb :
// valeur kOnEdge sur le contour, plus grande à l'intérieur, libérée par free().
void edt1d(const float *f, int n, float *d, int *v, float *z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; ++q) {
        float sx = ((f[q] + static_cast<float>(q * q)) - (f[v[k]] + static_cast<float>(v[k] * v[k]))) / static_cast<float>(2 * q - 2 * v[k]);
        while (sx <= z[k]) {
            --k;
            sx = ((f[q] + static_cast<float>(q * q)) - (f[v[k]] + static_cast<float>(v[k] * v[k]))) / static_cast<float>(2 * q - 2 * v[k]);
        }
        ++k;
        v[k] = q;
        z[k] = sx;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q)) ++k;
        const float dq = static_cast<float>(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

void edt2d(std::vector<float> &grid, int w, int h) {
    const int n = std::max(w, h);
    std::vector<float> f(static_cast<size_t>(n)), d(static_cast<size_t>(n)), z(static_cast<size_t>(n) + 1);
    std::vector<int> v(static_cast<size_t>(n));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[static_cast<size_t>(y)] = grid[static_cast<size_t>(y) * w + x];
        edt1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; ++y) grid[static_cast<size_t>(y) * w + x] = d[static_cast<size_t>(y)];
    }
    for (int y = 0; y < h; ++y) {
        float *row = &grid[static_cast<size_t>(y) * w];
        std::copy(row, row + w, f.begin());
        edt1d(f.data(), w, d.data(), v.data(), z.data());
        std::copy(d.begin(), d.begin() + w, row);
    }
}

unsigned char *glyphSdf(const stbtt_fontinfo *info, float scale, int index, int *outW, int *outH, int *outX, int *outY) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(info, index, scale, scale, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return nullptr;
    constexpr int ss = 4;
    const int w = x1 - x0 + 2 * kPad, h = y1 - y0 + 2 * kPad;
    const int xoff = x0 - kPad, yoff = y0 - kPad;
    const int W = w * ss, H = h * ss;
    int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
    stbtt_GetGlyphBitmapBox(info, index, scale * ss, scale * ss, &hx0, &hy0, &hx1, &hy1);
    const int bw = hx1 - hx0, bh = hy1 - hy0;
    std::vector<unsigned char> cover(static_cast<size_t>(bw) * bh, 0);
    stbtt_MakeGlyphBitmap(info, cover.data(), bw, bh, bw, scale * ss, scale * ss, index);
    // Masque intérieur dans la grande grille (le glyphe placé à sa position).
    const int ox = hx0 - xoff * ss, oy = hy0 - yoff * ss;
    std::vector<uint8_t> inside(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const int gx = x + ox, gy = y + oy;
            if (gx >= 0 && gy >= 0 && gx < W && gy < H && cover[static_cast<size_t>(y) * bw + x] >= 128)
                inside[static_cast<size_t>(gy) * W + gx] = 1;
        }
    constexpr float kInf = 1e20f;
    std::vector<float> toIn(static_cast<size_t>(W) * H), toOut(static_cast<size_t>(W) * H);
    for (size_t i = 0; i < inside.size(); ++i) {
        toIn[i] = inside[i] ? 0 : kInf;
        toOut[i] = inside[i] ? kInf : 0;
    }
    edt2d(toIn, W, H);
    edt2d(toOut, W, H);
    unsigned char *out = static_cast<unsigned char *>(malloc(static_cast<size_t>(w) * h));
    if (!out) return nullptr;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y * ss + ss / 2) * W + static_cast<size_t>(x * ss + ss / 2);
            // Distance signée au contour, en pixels de l'atlas (positive dedans).
            const float d = inside[i] ? (std::sqrt(toOut[i]) - 0.5f) / ss : -(std::sqrt(toIn[i]) - 0.5f) / ss;
            const float val = static_cast<float>(kOnEdge) + d * kDistScale;
            out[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(std::clamp(val, 0.0f, 255.0f));
        }
    *outW = w;
    *outH = h;
    *outX = xoff;
    *outY = yoff;
    return out;
}

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
            b.pixels = glyphSdf(&f->info, f->scale, index, &b.w, &b.h, &xoff, &yoff);
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
            free(b.pixels);
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
    if (dyn_ && cp >= 0x2000) {
        if (const Glyph *g = dyn_->get(cp)) return g;
    }
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
    s = i18n::tr(s);  // texte affiché dans la langue choisie
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
    s = i18n::tr(s);
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
            batch.sdfQuad(g->tex ? *g->tex : atlas_, pen + g->x0 * k, y + g->y0 * k, (g->x1 - g->x0) * k, (g->y1 - g->y0) * k, color, g->u0,
                          g->v0, g->u1, g->v1, dilate, soft);
        pen += g->advance * k + spacing;
        prev = g;
    }
    return width;
}

void Font::drawGlyphCentered(SpriteBatch &batch, const char *s, float cx, float cy, float size, uint32_t color, float angle, float sx,
                             float sy) const {
    if (!loaded() || !s || (color >> 24) == 0) return;
    const unsigned cp = decodeUtf8(s);
    const Glyph *g = find(cp);
    if (!g || g->x1 <= g->x0) return;
    const float k = size / bakePx_;
    const float w = (g->x1 - g->x0) * k * sx, h = (g->y1 - g->y0) * k * sy;
    const Texture &atlas = g->tex ? *g->tex : atlas_;
    if (angle != 0)
        batch.sdfQuadRotated(atlas, cx, cy, w, h, angle, color, g->u0, g->v0, g->u1, g->v1);
    else
        batch.sdfQuad(atlas, cx - w * 0.5f, cy - h * 0.5f, w, h, color, g->u0, g->v0, g->u1, g->v1, 0, 0);
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

std::string Font::ellipsize(float size, const std::string &original, float maxWidth) const {
    const std::string text = i18n::tr(original);
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

bool GlyphCache::load(const std::string &path, float bakePx, float hostAscent) {
    if (!fsu::readFile(path, ttf_, 32u << 20)) {
        LOG("text: police de secours introuvable: %s", path.c_str());
        return false;
    }
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0))) return false;
    info_.resize(sizeof(stbtt_fontinfo));
    memcpy(info_.data(), &info, sizeof(info));
    scale_ = stbtt_ScaleForPixelHeight(&info, bakePx);
    ascent_ = hostAscent;
    std::vector<uint8_t> zero(static_cast<size_t>(kSize) * kSize, 0);
    atlas_ = createAlphaTexture(kSize, kSize, zero.data(), true);
    LOG("text: police de secours %s (%.1f Mo), glyphes à la demande", path.c_str(), ttf_.size() / 1048576.0);
    return true;
}

void GlyphCache::shutdown() {
    destroyTexture(atlas_);
    glyphs_.clear();
}

bool GlyphCache::has(unsigned cp) const {
    if (info_.empty()) return false;
    const auto *info = reinterpret_cast<const stbtt_fontinfo *>(info_.data());
    return stbtt_FindGlyphIndex(info, static_cast<int>(cp)) != 0;
}

const Font::Glyph *GlyphCache::get(unsigned cp) {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    if (info_.empty() || !atlas_.id) return nullptr;
    const auto *info = reinterpret_cast<const stbtt_fontinfo *>(info_.data());
    const int index = stbtt_FindGlyphIndex(info, static_cast<int>(cp));
    if (index == 0) return nullptr;
    Font::Glyph g{};
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, index, &adv, &lsb);
    g.advance = static_cast<float>(adv) * scale_;
    g.index = index;
    g.fallback = true;
    g.tex = &atlas_;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char *px = glyphSdf(info, scale_, index, &w, &h, &xoff, &yoff);
    g.x0 = static_cast<float>(xoff);
    g.y0 = static_cast<float>(yoff) + ascent_;
    g.x1 = g.x0 + static_cast<float>(w);
    g.y1 = g.y0 + static_cast<float>(h);
    if (px) {
        if (penX_ + w + 1 > kSize) {
            penX_ = 1;
            penY_ += shelf_ + 1;
            shelf_ = 0;
        }
        if (penY_ + h + 1 > kSize) {
            // Atlas plein : on repart de zéro (les glyphes seront refaits au besoin).
            glyphs_.clear();
            penX_ = penY_ = 1;
            shelf_ = 0;
            LOG("text: atlas de secours plein, remis à zéro");
        }
        updateAlphaTextureRegion(atlas_, penX_, penY_, w, h, px);
        g.u0 = static_cast<float>(penX_) / kSize;
        g.v0 = static_cast<float>(penY_) / kSize;
        g.u1 = static_cast<float>(penX_ + w) / kSize;
        g.v1 = static_cast<float>(penY_ + h) / kSize;
        penX_ += w + 1;
        shelf_ = std::max(shelf_, h);
        free(px);
    } else {
        g.x1 = g.x0;
    }
    return &(glyphs_[cp] = g);
}

}  // namespace gfx
