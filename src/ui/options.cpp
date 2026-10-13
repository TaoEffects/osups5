// SPDX-License-Identifier: GPL-3.0-or-later
// Options façon "settings" de lazer : bandeau violet, sections, interrupteurs et
// curseurs roses. Manette (croix directionnelle), clavier (flèches) ou souris.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "app/game.h"
#include "core/anim.h"
#include "ui/theme.h"

namespace {

enum class Kind { Toggle, Slider, Choice };

struct Item {
    const char *section;
    const char *label;
    const char *help;
    Kind kind;
    std::function<std::string()> value;
    std::function<void(int dir)> change;
    std::function<float()> fill;      // 0..1 pour les curseurs
    std::function<void(float)> setFill;
};

constexpr float kListX = 560, kListW = 1000, kRowH = 92;
constexpr uint32_t kPurple = gfx::rgba(136, 102, 238);

class Options : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        enterTime_ = game.ctx.now;
        refreshAtEnter_ = game.ctx.settings->refreshHz;
        build();
    }

    void leave() override {
        Context &c = game.ctx;
        c.saveSettings();
        if (c.settings->refreshHz != refreshAtEnter_)
            game.toast("Fréquence " + std::to_string(c.settings->refreshHz) + " Hz : relance l'appli pour l'appliquer");
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        if (in.back) {
            c.play(c.sfx->uiBack);
            game.goTo(game.makeMainMenu());
            return;
        }
        const int n = static_cast<int>(items_.size());
        const int old = cursor_;
        if (in.up) cursor_ = (cursor_ + n - 1) % n;
        if (in.down) cursor_ = (cursor_ + 1) % n;
        if (in.wheel != 0) target_ = std::clamp(target_ - in.wheel * 120, 0.0f, maxScroll());
        if (in.left || in.right || in.confirm) {
            items_[static_cast<size_t>(cursor_)].change(in.left ? -1 : 1);
            changed();
        }
        // Souris : survol, clic (interrupteurs/choix), clic-glisser sur les curseurs.
        const bool held = in.raw && (in.raw->mouse.buttons & 1u);
        for (int i = 0; i < n; ++i) {
            const float y = rowY(i);
            if (!in.hover(kListX, y, kListW, kRowH - 10)) continue;
            if (in.mouseMoved) cursor_ = i;
            Item &it = items_[static_cast<size_t>(i)];
            if (it.kind == Kind::Slider && held && in.mouseX >= kListX + 560 && in.mouseX <= kListX + 900) {
                it.setFill((in.mouseX - (kListX + 560)) / 340.0f);
                c.applyVolumes();
            } else if (in.mouseClick && it.kind != Kind::Slider) {
                it.change(1);
                changed();
            }
        }
        // Sections (barre latérale).
        for (size_t s = 0; s < sections_.size(); ++s)
            if (in.clicked(0, 240 + static_cast<float>(s) * 100, 420, 90)) cursor_ = sectionStart(sections_[s]);
        if (cursor_ != old) {
            c.play(c.sfx->uiHover, 0.7f);
            if (!in.mouseActive) {
                const float y = rowY(cursor_) + target_;
                if (y - target_ > kVH - 240) target_ = y - (kVH - 240);
                if (y - target_ < 260) target_ = std::max(0.0f, y - 260);
            }
        }
        scroll_ = anim::approach(scroll_, target_, c.dt, 0.05f);
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        game.drawMenuBackground(0.8f);
        const float in = anim::outQuint(static_cast<float>((c.now - enterTime_) / 0.6));

        // Liste.
        const char *lastSection = "";
        for (size_t i = 0; i < items_.size(); ++i) {
            Item &it = items_[i];
            const float y = rowY(static_cast<int>(i));
            if (std::string(it.section) != lastSection) {
                lastSection = it.section;
                if (y - 60 > 170 && y < kVH - 60)
                    c.fonts->bold.draw(b, kListX, y - 58, 34, gfx::withAlpha(ui::lighten(kPurple, 0.3f), in), it.section);
            }
            if (y < 170 || y > kVH - 90) continue;
            const bool sel = static_cast<int>(i) == cursor_;
            const float x = kListX + (1 - in) * 300;
            ui::roundRect(c, x, y, kListW, kRowH - 10, 18, gfx::withAlpha(sel ? gfx::rgba(255, 255, 255, 28) : gfx::rgba(255, 255, 255, 10), in));
            if (sel) b.rect(x, y + 12, 6, kRowH - 34, ui::color::pink);
            c.fonts->semibold.draw(b, x + 32, y + 24, 30, gfx::withAlpha(ui::color::text, in), it.label);
            const std::string v = it.value();
            if (it.kind == Kind::Toggle) {
                const bool on = v == "on";
                ui::roundRect(c, x + kListW - 140, y + 22, 96, 40, 20, on ? ui::color::pink : gfx::rgba(255, 255, 255, 40));
                b.centered(c.tex->disc, on ? x + kListW - 64 : x + kListW - 120, y + 42, 30, ui::color::text);
            } else if (it.kind == Kind::Slider) {
                const float f = std::clamp(it.fill(), 0.0f, 1.0f);
                ui::roundRect(c, x + 560, y + 36, 340, 12, 6, gfx::rgba(255, 255, 255, 40));
                ui::roundRect(c, x + 560, y + 36, 340 * f, 12, 6, ui::color::pink);
                b.centered(c.tex->disc, x + 560 + 340 * f, y + 42, sel ? 34.0f : 28.0f, ui::color::text);
                b.centered(c.tex->disc, x + 560 + 340 * f, y + 42, sel ? 22.0f : 18.0f, ui::color::pink);
                c.fonts->bold.draw(b, x + kListW - 30, y + 24, 28, gfx::withAlpha(ui::color::text, in), v.c_str(), gfx::Align::Right);
            } else {
                ui::roundRect(c, x + kListW - 300, y + 18, 270, 48, 24, gfx::rgba(0, 0, 0, 100));
                c.fonts->bold.draw(b, x + kListW - 165, y + 26, 28, gfx::withAlpha(ui::color::text, in), v.c_str(), gfx::Align::Center);
                ui::icon(c, ui::Icon::Back, x + kListW - 280, y + 42, 20, ui::color::textDim);
                b.rotated(c.tex->arrow, x + kListW - 48, y + 42, 26, 26, 0, ui::color::textDim);
            }
        }

        // Barre latérale (sections) et aide.
        b.rect(0, 170, 440, kVH - 170, gfx::rgba(16, 14, 22, 230));
        const std::string current = items_[static_cast<size_t>(cursor_)].section;
        const ui::Icon icons[] = {ui::Icon::Settings, ui::Icon::Music, ui::Icon::Play, ui::Icon::Star};
        for (size_t s = 0; s < sections_.size(); ++s) {
            const float y = 240 + static_cast<float>(s) * 100;
            const bool on = current == sections_[s];
            if (on) ui::roundRect(c, 30, y, 380, 84, 20, gfx::rgba(255, 255, 255, 22));
            ui::icon(c, icons[s % 4], 80, y + 42, 36, on ? ui::color::pink : ui::color::textDim);
            c.fonts->bold.draw(b, 130, y + 24, 32, on ? ui::color::text : ui::color::textDim, sections_[s].c_str());
        }
        const Item &cur = items_[static_cast<size_t>(cursor_)];
        ui::roundRect(c, 1600, 300, 290, 420, 22, gfx::rgba(20, 16, 28, 220));
        wrap(cur.help, 1625, 325, 245, 24);

        ui::header(c, "Options", "Affichage, son, manette", kPurple, in);
        ui::hintBar(c, {{ui::PadIcon::DPad, "Choisir / régler"}, {ui::PadIcon::Circle, "Enregistrer et revenir"}}, in);
    }

private:
    float rowY(int i) const {
        float y = 280 - scroll_;
        const char *last = "";
        for (int k = 0; k <= i && k < static_cast<int>(items_.size()); ++k) {
            if (std::string(items_[static_cast<size_t>(k)].section) != last) {
                last = items_[static_cast<size_t>(k)].section;
                if (k > 0) y += 70;
            }
            if (k < i) y += kRowH;
        }
        return y;
    }
    float maxScroll() const { return std::max(0.0f, rowY(static_cast<int>(items_.size()) - 1) + scroll_ - (kVH - 300)); }
    int sectionStart(const std::string &s) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (s == items_[i].section) return static_cast<int>(i);
        return 0;
    }
    void changed() {
        game.ctx.applyVolumes();
        game.ctx.play(game.ctx.sfx->uiHover);
    }

    void wrap(const char *text, float x, float y, float width, float size) {
        Context &c = game.ctx;
        std::string line, word;
        const std::string s = std::string(text) + " ";
        for (char ch : s) {
            if (ch != ' ') {
                word += ch;
                continue;
            }
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (c.fonts->regular.measure(size, candidate.c_str()) > width && !line.empty()) {
                c.fonts->regular.draw(*c.batch, x, y, size, ui::color::textDim, line.c_str());
                y += size * 1.35f;
                line = word;
            } else {
                line = candidate;
            }
            word.clear();
        }
        if (!line.empty()) c.fonts->regular.draw(*c.batch, x, y, size, ui::color::textDim, line.c_str());
    }

    static std::string pct(float v) { return std::to_string(static_cast<int>(std::lround(v * 100))) + " %"; }

    void slider(const char *section, const char *label, const char *help, float &v, float lo, float hi, float step,
                std::function<std::string()> fmt) {
        items_.push_back({section, label, help, Kind::Slider, std::move(fmt),
                          [&v, lo, hi, step](int d) { v = std::clamp(std::round((v + step * d) / step) * step, lo, hi); },
                          [&v, lo, hi] { return (v - lo) / (hi - lo); },
                          [&v, lo, hi, step](float f) { v = std::clamp(std::round((lo + f * (hi - lo)) / step) * step, lo, hi); }});
    }
    void toggle(const char *section, const char *label, const char *help, bool &v) {
        items_.push_back({section, label, help, Kind::Toggle, [&v] { return std::string(v ? "on" : "off"); }, [&v](int) { v = !v; },
                          nullptr, nullptr});
    }

    void build() {
        Settings &s = *game.ctx.settings;
        items_.clear();
        items_.push_back({"Affichage", "Fréquence d'affichage",
                          "60 ou 120 images par seconde. Le 120 Hz demande un téléviseur compatible ; la PS5 ne sort pas "
                          "au-delà de 120 Hz. Prend effet au prochain lancement.",
                          Kind::Choice, [&s] { return std::to_string(s.refreshHz) + " Hz"; },
                          [&s](int) { s.refreshHz = s.refreshHz == 60 ? 120 : 60; }, nullptr, nullptr});
        slider("Affichage", "Assombrissement du fond", "Assombrit l'image de fond pendant les parties pour mieux voir les cercles.",
               s.backgroundDim, 0, 1, 0.05f, [&s] { return pct(s.backgroundDim); });
        toggle("Affichage", "Barre d'erreur de frappe", "Affiche en bas de l'écran si tes frappes sont en avance ou en retard.",
               s.showHitErrorBar);
        toggle("Affichage", "Infos de débogage", "Affiche les FPS, les appels de dessin et l'adresse du log réseau.", s.showDebug);
        slider("Son", "Volume général", "Volume de tout le jeu.", s.masterVolume, 0, 1, 0.05f, [&s] { return pct(s.masterVolume); });
        slider("Son", "Musique", "Volume de la musique.", s.musicVolume, 0, 1, 0.05f, [&s] { return pct(s.musicVolume); });
        slider("Son", "Effets", "Sons de frappe et de l'interface.", s.effectsVolume, 0, 1, 0.05f, [&s] { return pct(s.effectsVolume); });
        items_.push_back({"Son", "Décalage audio",
                          "Si tu frappes toujours en retard, augmente la valeur. L'écran de résultats propose une correction "
                          "automatique.",
                          Kind::Choice, [&s] { return (s.offsetMs > 0 ? "+" : "") + std::to_string(s.offsetMs) + " ms"; },
                          [&s](int d) { s.offsetMs = std::clamp(s.offsetMs + 5 * d, -300, 300); }, nullptr, nullptr});
        slider("Manette", "Vitesse du curseur",
               "Vitesse du curseur quand le stick gauche est poussé à fond. Le stick droit donne un mouvement fin.", s.cursorSpeed, 300,
               4000, 100, [&s] { return std::to_string(static_cast<int>(s.cursorSpeed)); });
        slider("Manette", "Courbe du stick", "Plus la valeur est haute, plus les petits mouvements sont précis.", s.cursorCurve, 1, 3,
               0.1f, [&s] {
                   char b[16];
                   snprintf(b, sizeof(b), "%.1f", s.cursorCurve);
                   return std::string(b);
               });
        items_.push_back({"Manette", "Pavé tactile",
                          "Absolu : le pavé correspond à l'aire de jeu, comme une tablette (sans lissage). Relatif : il "
                          "déplace le curseur comme un trackpad.",
                          Kind::Choice, [&s] { return std::string(s.touchpadAbsolute ? "Absolu" : "Relatif"); },
                          [&s](int) { s.touchpadAbsolute = !s.touchpadAbsolute; }, nullptr, nullptr});
        sections_.clear();
        for (const Item &it : items_)
            if (sections_.empty() || sections_.back() != it.section) sections_.push_back(it.section);
    }

    std::vector<Item> items_;
    std::vector<std::string> sections_;
    int cursor_ = 0;
    float scroll_ = 0, target_ = 0;
    double enterTime_ = 0;
    int refreshAtEnter_ = 60;
};

}  // namespace

std::unique_ptr<Screen> Game::makeOptions() { return std::make_unique<Options>(*this); }
