// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme.h"

#include <cmath>
#include <cstdio>

#include "core/anim.h"

namespace ui {
namespace {

struct Tri {
    float x, y, size, speed, shade;
};
std::vector<Tri> g_tris;

uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        const float va = static_cast<float>((a >> s) & 255), vb = static_cast<float>((b >> s) & 255);
        return static_cast<uint32_t>(va + (vb - va) * t + 0.5f) << s;
    };
    return ch(0) | ch(8) | ch(16) | ch(24);
}

}  // namespace

void init(Context &) {
    g_tris.clear();
    uint32_t seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < 46; ++i)
        g_tris.push_back({rnd() * kVW, rnd() * (kVH + 300), 60 + rnd() * 220, 15 + rnd() * 40, rnd()});
}

void shutdown() { g_tris.clear(); }

void roundRect(Context &ctx, float x, float y, float w, float h, float r, uint32_t c) {
    gfx::SpriteBatch &b = *ctx.batch;
    r = std::fmin(r, std::fmin(w, h) * 0.5f);
    if (r < 1) {
        b.rect(x, y, w, h, c);
        return;
    }
    const gfx::Texture &d = ctx.tex->disc;
    b.quad(d, x, y, r, r, c, 0, 0, 0.5f, 0.5f);
    b.quad(d, x + w - r, y, r, r, c, 0.5f, 0, 1, 0.5f);
    b.quad(d, x, y + h - r, r, r, c, 0, 0.5f, 0.5f, 1);
    b.quad(d, x + w - r, y + h - r, r, r, c, 0.5f, 0.5f, 1, 1);
    b.rect(x + r, y, w - 2 * r, h, c);
    b.rect(x, y + r, r, h - 2 * r, c);
    b.rect(x + w - r, y + r, r, h - 2 * r, c);
}

void padIcon(Context &ctx, PadIcon icon, float cx, float cy, float size, float alpha) {
    gfx::SpriteBatch &b = *ctx.batch;
    const float r = size * 0.5f;
    const uint32_t bg = gfx::withAlpha(gfx::rgba(20, 18, 28), 0.85f * alpha);
    auto label = [&](const char *t, float w) {
        roundRect(ctx, cx - w / 2, cy - r * 0.8f, w, r * 1.6f, r * 0.5f, bg);
        ctx.fonts->bold.draw(b, cx, cy - r * 0.62f, r * 1.05f, gfx::withAlpha(color::text, alpha), t, gfx::Align::Center);
    };
    switch (icon) {
    case PadIcon::L1: label("L1", size * 1.3f); return;
    case PadIcon::R1: label("R1", size * 1.3f); return;
    case PadIcon::L2: label("L2", size * 1.3f); return;
    case PadIcon::R2: label("R2", size * 1.3f); return;
    case PadIcon::Options: label("OPTIONS", size * 3.0f); return;
    default: break;
    }
    b.centered(ctx.tex->disc, cx, cy, size, bg);
    const float g = r * 0.5f, t = std::fmax(2.0f, size * 0.09f);
    switch (icon) {
    case PadIcon::Cross: {
        const uint32_t c = gfx::withAlpha(gfx::rgba(124, 178, 255), alpha);
        b.line(cx - g, cy - g, cx + g, cy + g, t, c);
        b.line(cx - g, cy + g, cx + g, cy - g, t, c);
        break;
    }
    case PadIcon::Circle:
        b.centered(ctx.tex->ringThick, cx, cy, g * 2.2f, gfx::withAlpha(gfx::rgba(255, 110, 110), alpha));
        break;
    case PadIcon::Square: {
        const uint32_t c = gfx::withAlpha(gfx::rgba(240, 140, 220), alpha);
        b.outline(cx - g * 0.85f, cy - g * 0.85f, g * 1.7f, g * 1.7f, t, c);
        break;
    }
    case PadIcon::Triangle: {
        const uint32_t c = gfx::withAlpha(gfx::rgba(100, 230, 190), alpha);
        const float h = g * 0.9f;
        b.line(cx, cy - h, cx + h, cy + h * 0.7f, t, c);
        b.line(cx + h, cy + h * 0.7f, cx - h, cy + h * 0.7f, t, c);
        b.line(cx - h, cy + h * 0.7f, cx, cy - h, t, c);
        break;
    }
    case PadIcon::DPad: {
        const uint32_t c = gfx::withAlpha(color::text, alpha);
        b.rect(cx - g * 0.3f, cy - g, g * 0.6f, g * 2, c);
        b.rect(cx - g, cy - g * 0.3f, g * 2, g * 0.6f, c);
        break;
    }
    case PadIcon::Stick:
        b.centered(ctx.tex->ringThick, cx, cy, g * 2.2f, gfx::withAlpha(color::text, alpha));
        b.centered(ctx.tex->disc, cx, cy, g * 0.9f, gfx::withAlpha(color::text, alpha));
        break;
    default: break;
    }
}

void hintBar(Context &ctx, const std::vector<std::pair<PadIcon, const char *>> &hints, float alpha) {
    gfx::SpriteBatch &b = *ctx.batch;
    b.gradient(0, kVH - 90, kVW, 90, gfx::rgba(0, 0, 0, 0), gfx::withAlpha(gfx::rgba(0, 0, 0), 0.7f * alpha));
    float x = 40;
    const float y = kVH - 42;
    for (const auto &h : hints) {
        const bool wide = h.first == PadIcon::Options;
        const bool shoulder = h.first == PadIcon::L1 || h.first == PadIcon::R1 || h.first == PadIcon::L2 || h.first == PadIcon::R2;
        const float iconW = wide ? 96.0f : (shoulder ? 44.0f : 34.0f);
        padIcon(ctx, h.first, x + iconW / 2, y, 34, alpha);
        x += iconW + 10;
        x += ctx.fonts->semibold.draw(b, x, y - 16, 26, gfx::withAlpha(color::text, alpha), h.second) + 36;
    }
}

void background(Context &ctx, const gfx::Texture *image, float dim, float px, float py) {
    gfx::SpriteBatch &b = *ctx.batch;
    b.rect(0, 0, kVW, kVH, color::bg);
    if (image && image->id) {
        const float bw = static_cast<float>(image->width), bh = static_cast<float>(image->height);
        const float k = std::fmax(kVW / bw, kVH / bh) * 1.06f;
        b.quad(*image, (kVW - bw * k) / 2 + px * 30, (kVH - bh * k) / 2 + py * 30, bw * k, bh * k, gfx::rgba(255, 255, 255));
        b.rect(0, 0, kVW, kVH, gfx::withAlpha(gfx::rgba(10, 8, 16), dim));
    }
    // Triangles qui montent lentement (signature visuelle du thème moderne).
    const float t = static_cast<float>(ctx.now);
    for (const Tri &tri : g_tris) {
        float y = std::fmod(tri.y - t * tri.speed, kVH + 400.0f);
        if (y < 0) y += kVH + 400.0f;
        y -= 200;
        const uint32_t c = gfx::withAlpha(mix(gfx::rgba(255, 102, 170), gfx::rgba(102, 120, 255), tri.shade), 0.05f + 0.05f * tri.shade);
        b.quad(ctx.tex->triangle, tri.x + px * 15 - tri.size / 2, y + py * 15, tri.size, tri.size * 0.87f, c);
    }
}

uint32_t starColor(float s) {
    struct Stop {
        float at;
        uint32_t c;
    };
    static const Stop stops[] = {{0.1f, gfx::rgba(170, 170, 170)}, {1.5f, gfx::rgba(79, 192, 255)},
                                 {2.0f, gfx::rgba(79, 255, 213)},  {2.5f, gfx::rgba(124, 255, 79)},
                                 {3.3f, gfx::rgba(246, 240, 92)},  {4.2f, gfx::rgba(255, 128, 104)},
                                 {4.9f, gfx::rgba(255, 78, 111)},  {5.8f, gfx::rgba(198, 69, 184)},
                                 {6.7f, gfx::rgba(101, 99, 222)},  {7.7f, gfx::rgba(24, 21, 142)},
                                 {9.0f, gfx::rgba(10, 10, 10)}};
    if (s < 0) return gfx::rgba(150, 150, 160);
    if (s <= stops[0].at) return stops[0].c;
    for (size_t i = 1; i < sizeof(stops) / sizeof(stops[0]); ++i)
        if (s <= stops[i].at) return mix(stops[i - 1].c, stops[i].c, (s - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return stops[sizeof(stops) / sizeof(stops[0]) - 1].c;
}

std::string formatTime(double ms) {
    const int total = static_cast<int>(ms / 1000.0 + 0.5);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

}  // namespace ui

namespace ui {

uint32_t lighten(uint32_t c, float k) { return mix(c, gfx::rgba(255, 255, 255, static_cast<uint8_t>(c >> 24)), k); }
uint32_t darken(uint32_t c, float k) { return mix(c, gfx::rgba(0, 0, 0, static_cast<uint8_t>(c >> 24)), k); }

void skewed(Context &ctx, float x, float y, float w, float h, float skew, uint32_t c) {
    const float xy[8] = {x + skew, y, x + w + skew, y, x + w, y + h, x, y + h};
    ctx.batch->quad4(ctx.batch->white(), xy, c);
}

void trianglePanel(Context &ctx, float x, float y, float w, float h, uint32_t base, int seed, float alpha) {
    gfx::SpriteBatch &b = *ctx.batch;
    b.rect(x, y, w, h, gfx::withAlpha(base, alpha));
    uint32_t s = static_cast<uint32_t>(seed) * 2654435761u + 7;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    };
    const int count = std::max(6, static_cast<int>(w * h / 9000.0f));
    const float t = static_cast<float>(ctx.now);
    for (int i = 0; i < count && i < 160; ++i) {
        const float size = h * (0.25f + rnd() * 0.6f);
        const float tx = x + rnd() * std::max(1.0f, w - size);
        const float speed = 0.04f + rnd() * 0.08f;
        const float span = h + size;
        float ty = std::fmod(rnd() * span - t * speed * h, span);
        if (ty < 0) ty += span;
        ty = y + h - ty;
        // Fondu près des bords haut et bas pour masquer le débordement.
        const float edge = std::fmin(ty - y, y + h - (ty + size * 0.87f));
        const float a = anim::clamp01((edge + size * 0.4f) / (size * 0.4f));
        const float shade = rnd();
        const uint32_t c = shade < 0.5f ? lighten(base, 0.05f + shade * 0.2f) : darken(base, 0.05f + (shade - 0.5f) * 0.3f);
        if (a > 0.01f) b.quad(ctx.tex->triangle, tx, ty, size, size * 0.87f, gfx::withAlpha(c, a * alpha * 0.9f));
    }
}

void header(Context &ctx, const char *title, const char *subtitle, uint32_t base, float alpha) {
    gfx::SpriteBatch &b = *ctx.batch;
    trianglePanel(ctx, 0, 0, kVW, 170, darken(base, 0.35f), 99, alpha);
    b.gradient(0, 120, kVW, 50, gfx::rgba(0, 0, 0, 0), gfx::withAlpha(gfx::rgba(0, 0, 0), 0.35f * alpha));
    b.rect(0, 166, kVW, 4, gfx::withAlpha(base, alpha));
    ctx.fonts->bold.drawShadow(b, 80, 34, 64, gfx::withAlpha(color::text, alpha), title);
    if (subtitle) ctx.fonts->semibold.drawShadow(b, 82, 108, 26, gfx::withAlpha(lighten(base, 0.6f), alpha), subtitle);
}

void sideFlashes(Context &ctx, float intensity, uint32_t c) {
    if (intensity <= 0.01f) return;
    gfx::SpriteBatch &b = *ctx.batch;
    b.setBlend(gfx::Blend::Additive);
    const float w = 260;
    // Dégradé horizontal approché par 16 bandes.
    for (int i = 0; i < 16; ++i) {
        const float k = 1.0f - static_cast<float>(i) / 16.0f;
        b.rect(static_cast<float>(i) * w / 16, 0, w / 16 + 1, kVH, gfx::withAlpha(c, intensity * 0.45f * k * k));
        b.rect(kVW - static_cast<float>(i + 1) * w / 16, 0, w / 16 + 1, kVH, gfx::withAlpha(c, intensity * 0.45f * k * k));
    }
    b.setBlend(gfx::Blend::Alpha);
}

void visualiser(Context &ctx, float cx, float cy, float radius, float alpha) {
    if (!ctx.music->playing()) return;
    gfx::SpriteBatch &b = *ctx.batch;
    const auto spec = ctx.music->spectrum();
    const int bars = 160;
    const float rot = static_cast<float>(ctx.now) * 0.15f;
    b.setBlend(gfx::Blend::Additive);
    for (int i = 0; i < bars; ++i) {
        // Le spectre est répété 2 fois et symétrique, comme dans lazer.
        const int k = i % (bars / 2);
        const int band = k < bars / 4 ? k : bars / 2 - 1 - k;
        const float v = spec[static_cast<size_t>(band * audio::Music::kBands / (bars / 4) % audio::Music::kBands)];
        const float len = 6 + v * radius * 0.7f;
        const float a = rot + static_cast<float>(i) / bars * 6.2831853f;
        const float ca = std::cos(a), sa = std::sin(a);
        b.line(cx + ca * radius, cy + sa * radius, cx + ca * (radius + len), cy + sa * (radius + len), 7,
               gfx::withAlpha(color::text, alpha * 0.22f));
    }
    b.setBlend(gfx::Blend::Alpha);
}

void cursor(Context &ctx, float x, float y, bool pressed) {
    gfx::SpriteBatch &b = *ctx.batch;
    const float s = pressed ? 0.85f : 1.0f;
    b.centered(ctx.tex->glow, x, y, 70 * s, gfx::withAlpha(color::pink, 0.5f));
    b.centered(ctx.tex->disc, x, y, 34 * s, color::text);
    b.centered(ctx.tex->disc, x, y, 26 * s, color::pink);
}

std::vector<float> footerLayout(Context &ctx, const std::vector<const char *> &labels, float startX, std::vector<float> &widths) {
    std::vector<float> xs;
    widths.clear();
    float x = startX;
    for (const char *l : labels) {
        const float w = ctx.fonts->bold.measure(30, l) + (std::string(l) == "Options" ? 170.0f : 120.0f);
        xs.push_back(x);
        widths.push_back(w);
        x += w + 8;
    }
    return xs;
}

void footerButton(Context &ctx, float x, float w, const char *label, uint32_t c, PadIcon pad, bool hovered) {
    gfx::SpriteBatch &b = *ctx.batch;
    const float y = kVH - kFooterH;
    const float lift = hovered ? 10.0f : 0.0f;
    skewed(ctx, x, y - lift, w, kFooterH + lift, 16, hovered ? lighten(c, 0.1f) : darken(c, 0.25f));
    b.rect(x + 16, y - lift, w - 8, 5, lighten(c, 0.4f));
    const float iconW = pad == PadIcon::Options ? 100.0f : 40.0f;
    padIcon(ctx, pad, x + 26 + iconW / 2, y - lift + kFooterH / 2 + 2, 34);
    ctx.fonts->bold.draw(b, x + 36 + iconW, y - lift + 20, 30, color::text, label);
}

void icon(Context &ctx, Icon ic, float cx, float cy, float s, uint32_t c) {
    gfx::SpriteBatch &b = *ctx.batch;
    const float t = std::fmax(3.0f, s * 0.1f), h = s * 0.5f;
    switch (ic) {
    case Icon::Play: {
        const float xy[8] = {cx - h * 0.6f, cy - h * 0.8f, cx + h * 0.9f, cy, cx + h * 0.9f, cy, cx - h * 0.6f, cy + h * 0.8f};
        b.quad4(b.white(), xy, c);
        break;
    }
    case Icon::Download:
        b.line(cx, cy - h, cx, cy + h * 0.4f, t, c);
        b.line(cx - h * 0.55f, cy - h * 0.1f, cx, cy + h * 0.45f, t, c);
        b.line(cx + h * 0.55f, cy - h * 0.1f, cx, cy + h * 0.45f, t, c);
        b.line(cx - h * 0.8f, cy + h * 0.85f, cx + h * 0.8f, cy + h * 0.85f, t, c);
        break;
    case Icon::Settings:
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.785f;
            b.line(cx + std::cos(a) * h * 0.55f, cy + std::sin(a) * h * 0.55f, cx + std::cos(a) * h * 0.95f, cy + std::sin(a) * h * 0.95f,
                   t * 1.4f, c);
        }
        b.centered(ctx.tex->ringThick, cx, cy, s * 0.8f, c);
        break;
    case Icon::Exit:
        b.centered(ctx.tex->ringThick, cx, cy + h * 0.1f, s * 0.9f, c);
        b.rect(cx - t * 1.5f, cy - h * 0.6f, t * 3, h * 0.6f, darken(c, 1.0f));
        b.line(cx, cy - h * 0.95f, cx, cy, t, c);
        break;
    case Icon::Music:
        b.line(cx + h * 0.3f, cy - h * 0.8f, cx + h * 0.3f, cy + h * 0.5f, t, c);
        b.line(cx + h * 0.3f, cy - h * 0.8f, cx + h * 0.8f, cy - h * 0.5f, t, c);
        b.centered(ctx.tex->disc, cx, cy + h * 0.55f, h * 0.75f, c);
        break;
    case Icon::Clock:
        b.centered(ctx.tex->ringThick, cx, cy, s, c);
        b.line(cx, cy, cx, cy - h * 0.6f, t, c);
        b.line(cx, cy, cx + h * 0.45f, cy, t, c);
        break;
    case Icon::Star:
        for (int i = 0; i < 5; ++i) {
            const float a = -1.5708f + static_cast<float>(i) * 1.2566f;
            const float a2 = a + 2.5133f;
            b.line(cx + std::cos(a) * h, cy + std::sin(a) * h, cx + std::cos(a2) * h, cy + std::sin(a2) * h, t, c);
        }
        break;
    case Icon::Search:
        b.centered(ctx.tex->ringThick, cx - h * 0.15f, cy - h * 0.15f, s * 0.75f, c);
        b.line(cx + h * 0.25f, cy + h * 0.25f, cx + h * 0.8f, cy + h * 0.8f, t * 1.2f, c);
        break;
    case Icon::Back:
        b.line(cx - h * 0.4f, cy, cx + h * 0.3f, cy - h * 0.7f, t * 1.2f, c);
        b.line(cx - h * 0.4f, cy, cx + h * 0.3f, cy + h * 0.7f, t * 1.2f, c);
        break;
    case Icon::Random:
        b.outline(cx - h * 0.8f, cy - h * 0.8f, h * 1.6f, h * 1.6f, t, c);
        b.centered(ctx.tex->disc, cx - h * 0.35f, cy - h * 0.35f, h * 0.4f, c);
        b.centered(ctx.tex->disc, cx + h * 0.35f, cy + h * 0.35f, h * 0.4f, c);
        b.centered(ctx.tex->disc, cx, cy, h * 0.4f, c);
        break;
    case Icon::Mods:
        b.rotated(b.white(), cx, cy, h * 1.2f, h * 1.2f, 0.785f, c);
        b.rotated(b.white(), cx, cy, h * 0.6f, h * 0.6f, 0.785f, darken(c, 0.6f));
        break;
    case Icon::Circles:
        b.centered(ctx.tex->ringThick, cx, cy, s * 0.9f, c);
        b.centered(ctx.tex->disc, cx, cy, s * 0.35f, c);
        break;
    case Icon::Sliders:
        b.line(cx - h * 0.6f, cy + h * 0.3f, cx + h * 0.6f, cy - h * 0.3f, s * 0.42f, c);
        b.line(cx - h * 0.6f, cy + h * 0.3f, cx + h * 0.6f, cy - h * 0.3f, s * 0.2f, darken(c, 0.5f));
        break;
    }
}

}  // namespace ui
