// SPDX-License-Identifier: GPL-3.0-or-later
// Intro au lancement, sur le modèle d'IntroTriangles de lazer (minutage et
// mouvements repris de son code, contenu et identité visuelle propres) :
//   200 ms « bien », 400 « bienvenue », 700 « bienvenue sur », 900 nom du jeu
//   1060 triangles « glitch », 1450 / 1650 / 1850 icônes qui grossissent,
//   2080 grand triangle révélé, 3000 logo + flash blanc -> menu.
#include <cmath>
#include <cstdio>

#include "app/game.h"
#include "ui/lz.h"

namespace {

using namespace lz;

class Intro : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        start_ = ms(c);
        game.backgroundVisible = false;
        game.setBackground(1, 0, 0);
        game.toolbar.shown.snap(0);
        game.logo.opacity.snap(0);
        game.logo.x.snap(kVW / 2);
        game.logo.y.snap(kVH / 2);
        game.logo.scale.snap(1);
        game.logo.interactive = false;
    }

    double leave() override { return 0; }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const double t = ms(c) - start_;
        // Sons de l'intro : petits tics sur les mots, montée sur les icônes.
        const double cues[] = {200, 400, 700, 900, 1450, 1650, 1850, 2080};
        while (cue_ < 8 && t >= cues[cue_]) {
            if (cue_ < 4)
                c.play(c.sfx->uiHover, 0.9f);
            else
                c.play(c.sfx->uiSelect, 0.6f + 0.15f * static_cast<float>(cue_ - 4));
            ++cue_;
        }
        if ((in.confirm || in.back || in.mouseClick || in.anyKey) && t < 2900) {
            // Passer l'intro : on saute directement au flash.
            start_ = ms(c) - 3000;
            cue_ = 8;
        }
        if (ms(c) - start_ >= 3000 && !done_) finish();
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c) - start_;
        b.rect(0, 0, kVW, kVH, col::black);
        if (t < 1450) drawWelcome(t);
        if (t >= 1060 && t < 1450) drawGlitch(t);
        if (t >= 1450 && t < 2080) drawIcons(t);
        if (t >= 2080 && t < 3000) drawReveal(t);
    }

private:
    void drawWelcome(double t) {
        Context &c = game.ctx;
        const char *txt = t < 200 ? "" : t < 400 ? "bien" : t < 700 ? "bienvenue" : t < 900 ? "bienvenue sur" : "bienvenue sur rhythm";
        if (!*txt) return;
        // Espacement 5 -> 50 sur 5 s, linéaire, à partir de 900 ms.
        const float spacing = 5 * U + (t >= 900 ? static_cast<float>((t - 900) / 5000.0) * 45 * U : 0);
        const float size = 42 * U;
        c.fonts->light.draw(*c.batch, kVW / 2, kVH / 2 - size * 0.62f - 5 * U, size, col::white, txt, gfx::Align::Center, spacing);
    }

    void drawGlitch(double t) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        // Un triangle toutes les 22 ms dans une zone de 0,4 x 0,16 de l'écran, visible 120 ms.
        const float aw = kVW * 0.4f, ah = kVH * 0.16f, ax = (kVW - aw) / 2, ay = (kVH - ah) / 2;
        b.setBlend(gfx::Blend::Additive);
        const int last = static_cast<int>((t - 1060) / 22);
        for (int i = std::max(0, last - 6); i <= last; ++i) {
            const double born = 1060 + i * 22.0;
            const float age = static_cast<float>(t - born);
            if (age < 0 || age > 120) continue;
            uint32_t h = static_cast<uint32_t>(i) * 2654435761u + 12345u;
            auto rnd = [&]() {
                h ^= h << 13;
                h ^= h >> 17;
                h ^= h << 5;
                return static_cast<float>(h & 0xFFFFFF) / 16777216.0f;
            };
            const float size = (rnd() + 0.2f) * 80 * U;
            const float x = ax + rnd() * aw, y = ay + rnd() * ah;
            const bool outline = rnd() < 0.5f;
            b.triangle(x + size / 2, y + size * 0.55f, size, alpha(col::white, 1 - age / 120), outline ? 2.5f * U : 0);
        }
        b.setBlend(gfx::Blend::Alpha);
    }

    void drawIcons(double t) {
        Context &c = game.ctx;
        // Les « modes de jeu » : quatre icônes, de plus en plus grosses et serrées.
        float scale = 1, spacing = 200 * U;
        if (t >= 1650) scale = 2, spacing = 30 * U;
        if (t >= 1850) scale = 4, spacing = 10 * U;
        float outer = 1;
        if (t < 1850)
            outer = 1 + (0.8f - 1) * static_cast<float>(std::min(1.0, (t - 1450) / 1000.0));
        else
            outer = (1 + (0.8f - 1) * 0.4f) + (1.3f - 0.92f) * static_cast<float>(std::min(1.0, (t - 1850) / 1000.0));
        const char *glyphs[4] = {fa::bullseye, fa::music, fa::circle_play, fa::star};
        const float size = 30 * U * scale * outer, gap = spacing * outer;
        const float total = size * 4 + gap * 3;
        for (int i = 0; i < 4; ++i)
            icon(c, glyphs[i], kVW / 2 - total / 2 + size / 2 + static_cast<float>(i) * (size + gap), kVH / 2, size, col::white);
    }

    void drawReveal(double t) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float p = static_cast<float>(std::min(1.0, (t - 2080) / 920.0));
        float s = 1.2f;
        if (t >= 2080 + 644) s = 1.2f + (0.4f - 1.2f) * anim::ease(Ease::InQuint, static_cast<float>((t - 2724) / 276.0));
        const float parent = 1.2f + (1.0f - 1.2f) * anim::ease(Ease::In, p);
        const float side = 560 * U * s * parent;
        const float cx = kVW / 2, cy = kVH / 2 + side * 0.1f;
        // Révélation de bas en haut : fond gris puis contour blanc.
        const float h = side * 0.866f;
        const float top = cy - h * 0.667f;
        b.pushClip(0, top + h * (1 - p), kVW, h * p + 4);
        b.triangle(cx, cy, side, hex(0x999999), side * 0.06f);
        b.popClip();
        const float p2 = std::max(0.0f, std::min(1.0f, p * 1.25f - 0.1f));
        b.pushClip(0, top + h * (1 - p2), kVW, h * p2 + 4);
        b.triangle(cx, cy, side * 0.82f, col::white, side * 0.025f);
        b.popClip();
    }

    void finish() {
        done_ = true;
        Context &c = game.ctx;
        game.logo.opacity.snap(1);
        game.logo.x.snap(kVW / 2);
        game.logo.y.snap(kVH / 2);
        game.logo.scale.snap(1);
        game.logo.flash.fromTo(ms(c), 0, 0, 1);
        game.backgroundVisible = true;
        game.setBackground(0, 0, 0);
        game.introFlash = true;
        c.play(c.sfx->uiOpen);
        game.goTo(c.settings->firstRunDone ? game.makeMainMenu() : game.makeFirstRun());
    }

    double start_ = 0;
    int cue_ = 0;
    bool done_ = false;
};

}  // namespace

std::unique_ptr<Screen> Game::makeIntro() { return std::make_unique<Intro>(*this); }
