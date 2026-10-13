// SPDX-License-Identifier: GPL-3.0-or-later
// Écran d'accueil façon lazer : logo rose qui bat au rythme de la musique, entouré
// d'un visualiseur ; une pression l'envoie à gauche et déplie la barre de boutons.
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "app/game.h"
#include "core/anim.h"
#include "game/library.h"
#include "ui/theme.h"
#include "version.h"

namespace {

struct Button {
    const char *label;
    ui::Icon icon;
    uint32_t color;
};

const Button kButtons[] = {
    {"Jouer", ui::Icon::Play, ui::color::purple},
    {"Télécharger", ui::Icon::Download, gfx::rgba(52, 160, 230)},
    {"Options", ui::Icon::Settings, gfx::rgba(255, 170, 30)},
    {"Quitter", ui::Icon::Exit, gfx::rgba(235, 74, 74)},
};
constexpr int kCount = 4;
constexpr float kBarH = 120;

class MainMenu : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        enterTime_ = game.ctx.now;
        if (!game.ctx.music->playing()) game.playPreview();
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        if (!previewStarted_ && !c.library->scanning()) {
            previewStarted_ = true;
            if (!c.music->playing()) game.playPreview();
        }
        const float logoX = open_ ? 470.0f : kVW / 2, logoY = kVH / 2;
        const bool overLogo = in.mouseActive && std::hypot(in.mouseX - logoX, in.mouseY - logoY) < 230;

        if (!open_) {
            if (in.confirm || (in.anyKey && !in.back) || (in.mouseClick && overLogo) || in.right || in.down) {
                open_ = true;
                openTime_ = c.now;
                c.play(c.sfx->uiOpen);
            }
        } else {
            // Boutons : souris (survol/clic), croix directionnelle ou flèches.
            for (int i = 0; i < kCount; ++i) {
                const ButtonRect r = rect(i);
                if (in.mouseActive && in.hover(r.x, r.y, r.w, r.h) && selected_ != i) {
                    selected_ = i;
                    c.play(c.sfx->uiHover, 0.7f);
                }
                if (in.clicked(r.x, r.y, r.w, r.h)) activate(i);
            }
            if (in.left || in.up) {
                selected_ = (selected_ + kCount - 1) % kCount;
                c.play(c.sfx->uiHover, 0.8f);
            }
            if (in.right || in.down) {
                selected_ = (selected_ + 1) % kCount;
                c.play(c.sfx->uiHover, 0.8f);
            }
            if (in.confirm) activate(selected_);
            if (in.mouseClick && overLogo) activate(0);  // le logo lance "Jouer", comme dans lazer
            if (in.back) {
                open_ = false;
                openTime_ = c.now;
                c.play(c.sfx->uiBack);
            }
        }
        if (in.options) game.goTo(game.makeOptions());
        for (int i = 0; i < kCount; ++i) hover_[i] = anim::approach(hover_[i], i == selected_ && open_ ? 1.0f : 0.0f, c.dt, 0.04f);
        openAnim_ = anim::approach(openAnim_, open_ ? 1.0f : 0.0f, c.dt, 0.07f);
        const float level = c.music->playing() ? c.music->level() : 0;
        loud_ = anim::approach(loud_, level, c.dt, level > loud_ ? 0.02f : 0.15f);
        logoHover_ = anim::approach(logoHover_, overLogo ? 1.0f : 0.0f, c.dt, 0.05f);
        if (c.beatIndex != lastBeat_) {
            lastBeat_ = c.beatIndex;
            ripples_[rippleNext_++ % 4] = c.now;
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float t = static_cast<float>(c.now);
        const UiInput &in = game.input();
        const float px = in.mouseActive ? (in.mouseX / kVW - 0.5f) : std::sin(t * 0.2f) * 0.3f;
        const float py = in.mouseActive ? (in.mouseY / kVH - 0.5f) : std::cos(t * 0.17f) * 0.3f;
        game.drawMenuBackground(0.35f, -px, -py);
        const float appear = anim::outQuint(static_cast<float>((c.now - enterTime_) / 0.9));

        // Flashs latéraux : forts sur le premier temps de la mesure, légers sinon.
        const bool downbeat = c.beatIndex % 4 == 0;
        ui::sideFlashes(c, c.beatPulse * (downbeat ? 0.9f : 0.35f) * appear, downbeat ? ui::color::pink : ui::color::text);

        // --- Barre de boutons ----------------------------------------------------------
        const float o = anim::outQuint(openAnim_);
        if (o > 0.01f) {
            const float barY = kVH / 2 - kBarH / 2;
            b.rect(0, barY, kVW, kBarH, gfx::withAlpha(gfx::rgba(20, 18, 26), 0.85f * o));
            for (int i = 0; i < kCount; ++i) {
                const ButtonRect r = rect(i);
                const float h = hover_[i];
                const float stagger = anim::outQuint(std::fmax(0.0f, (static_cast<float>(c.now - openTime_) - 0.05f * i) / 0.5f));
                const float k = open_ ? stagger * o : o;
                const float bx = r.x - (1 - k) * 300;
                const uint32_t col = kButtons[i].color;
                const float pulse = h * c.beatPulse * 6;
                ui::skewed(c, bx, r.y - h * 14 - pulse, r.w, r.h + h * 28 + pulse * 2, 22,
                           gfx::withAlpha(h > 0.5f ? ui::lighten(col, 0.1f) : col, k));
                if (h > 0.01f)
                    ui::trianglePanel(c, bx + 24, r.y - h * 14 + 6, r.w - 30, r.h + h * 28 - 12, col, 10 + i, h * k * 0.6f);
                ui::icon(c, kButtons[i].icon, bx + r.w / 2 + 11, r.y + r.h / 2 - 16 - h * 6, 40 + h * 10,
                         gfx::withAlpha(ui::color::text, k));
                c.fonts->bold.drawShadow(b, bx + r.w / 2 + 11, r.y + r.h / 2 + 14 + h * 4, 26 + h * 4, gfx::withAlpha(ui::color::text, k),
                                         kButtons[i].label, gfx::Align::Center);
            }
        }

        // --- Logo ------------------------------------------------------------------------
        const float logoX = anim::lerp(kVW / 2, 470.0f, o), logoY = kVH / 2;
        const float base = anim::lerp(1.0f, 0.78f, o);
        const float beat = 1.0f + 0.035f * c.beatPulse + std::fmin(0.05f, loud_ * 0.15f) + logoHover_ * 0.04f;
        const float R = 230 * base * beat * (0.6f + 0.4f * appear);
        ui::visualiser(c, logoX, logoY, R * 0.98f, appear);
        // Ondes qui partent du logo à chaque temps.
        for (double rt : ripples_) {
            const float k = static_cast<float>((c.now - rt) / 0.8);
            if (k < 0 || k > 1) continue;
            b.centered(c.tex->ring, logoX, logoY, R * 2 * (1 + k * 0.35f), gfx::withAlpha(ui::color::text, (1 - k) * 0.35f * appear));
        }
        b.setBlend(gfx::Blend::Additive);
        b.centered(c.tex->glow, logoX, logoY, R * 3.0f, gfx::withAlpha(ui::color::pink, (0.25f + 0.2f * c.beatPulse) * appear));
        b.setBlend(gfx::Blend::Alpha);
        b.centered(c.tex->disc, logoX, logoY, R * 2.0f, gfx::withAlpha(ui::color::text, appear));
        b.centered(c.tex->disc, logoX, logoY, R * 1.86f, gfx::withAlpha(ui::color::pinkDark, appear));
        b.centered(c.tex->disc, logoX, logoY, R * 1.8f, gfx::withAlpha(ui::color::pink, appear));
        // Triangles qui défilent dans le logo (approximativement à l'intérieur du disque).
        for (int i = 0; i < 26; ++i) {
            const float seed = static_cast<float>(i) * 12.9898f;
            const float fx = std::fmod(std::sin(seed) * 43758.5f, 1.0f);
            const float sz = (0.25f + std::fabs(std::fmod(std::sin(seed * 2.1f) * 9341.3f, 0.45f))) * R;
            float fy = std::fmod(std::fabs(std::sin(seed * 3.7f)) * 2.0f - t * 0.08f * (1 + i % 3), 2.0f);
            if (fy < 0) fy += 2.0f;
            const float tx = logoX + fx * R * 0.7f, ty = logoY + (fy - 1.0f) * R;
            const float d = std::hypot(tx - logoX, ty + sz * 0.4f - logoY) + sz * 0.55f;
            if (d > R * 0.9f) continue;
            const uint32_t sh = i % 2 ? ui::lighten(ui::color::pink, 0.15f) : ui::darken(ui::color::pink, 0.12f);
            b.quad(c.tex->triangle, tx - sz / 2, ty, sz, sz * 0.87f, gfx::withAlpha(sh, appear * anim::clamp01((R * 0.9f - d) / 40)));
        }
        c.fonts->bold.drawShadow(b, logoX, logoY - R * 0.27f, R * 0.46f, gfx::withAlpha(ui::color::text, appear), "rhythm!",
                                 gfx::Align::Center);

        // --- Barre du haut (heure, musique), façon barre d'outils de lazer ---------------
        b.rect(0, 0, kVW, 64, gfx::withAlpha(gfx::rgba(20, 18, 26), 0.75f * appear));
        c.fonts->bold.draw(b, 30, 14, 32, gfx::withAlpha(ui::color::pink, appear), "rhythm-ps5");
        char line[300];
        const time_t now = time(nullptr);
        const tm *lt = localtime(&now);
        if (lt) {
            strftime(line, sizeof(line), "%H:%M", lt);
            ui::icon(c, ui::Icon::Clock, kVW - 140, 32, 26, gfx::withAlpha(ui::color::text, appear));
            c.fonts->semibold.draw(b, kVW - 115, 16, 28, gfx::withAlpha(ui::color::text, appear), line);
        }
        if (const game::SetInfo *s = game.currentSet()) {
            snprintf(line, sizeof(line), "%s - %s", s->artist.c_str(), s->title.c_str());
            const std::string np = c.fonts->semibold.ellipsize(26, line, 900);
            const float w = c.fonts->semibold.measure(26, np.c_str());
            ui::icon(c, ui::Icon::Music, kVW - 220 - w, 32, 26, gfx::withAlpha(ui::color::pink, appear));
            c.fonts->semibold.draw(b, kVW - 190, 17, 26, gfx::withAlpha(ui::color::text, appear), np.c_str(), gfx::Align::Right);
        }
        if (c.library->scanning())
            snprintf(line, sizeof(line), "Lecture de la bibliothèque… %.0f%%", c.library->scanProgress() * 100);
        else
            snprintf(line, sizeof(line), "%zu maps installées", c.library->sets().size());
        c.fonts->semibold.draw(b, 330, 18, 26, gfx::withAlpha(ui::color::textDim, appear), line);

        if (!open_) {
            const float blink = 0.55f + 0.45f * std::sin(t * 3);
            c.fonts->semibold.drawShadow(b, kVW / 2, kVH - 150, 30, gfx::withAlpha(ui::color::text, blink * appear * (1 - o)),
                                         "Appuie sur Croix (ou clique sur le logo)", gfx::Align::Center);
        }
        c.fonts->regular.drawShadow(b, 30, kVH - 40, 20, gfx::withAlpha(ui::color::textDim, 0.7f * appear),
                                    RHYTHM_APP_NAME " " RHYTHM_VERSION " · projet libre et non officiel");
    }

private:
    struct ButtonRect {
        float x, y, w, h;
    };
    ButtonRect rect(int i) const {
        const float w = 250, gap = 6;
        return {720 + static_cast<float>(i) * (w + gap), kVH / 2 - kBarH / 2, w, kBarH};
    }

    void activate(int i) {
        Context &c = game.ctx;
        c.play(c.sfx->uiSelect);
        selected_ = i;
        switch (i) {
        case 0: game.goTo(game.makeSongSelect()); break;
        case 1: game.goTo(game.makeDownload()); break;
        case 2: game.goTo(game.makeOptions()); break;
        default: game.requestExit(); break;
        }
    }

    bool open_ = false;
    int selected_ = 0;
    float hover_[kCount] = {};
    float openAnim_ = 0, loud_ = 0, logoHover_ = 0;
    double enterTime_ = 0, openTime_ = 0;
    double ripples_[4] = {-10, -10, -10, -10};
    int rippleNext_ = 0;
    long lastBeat_ = 0;
    bool previewStarted_ = false;
};

}  // namespace

std::unique_ptr<Screen> Game::makeMainMenu() { return std::make_unique<MainMenu>(*this); }
