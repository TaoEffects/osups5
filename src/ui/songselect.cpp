// SPDX-License-Identifier: GPL-3.0-or-later
// Sélection de map façon lazer : encart incliné avec l'image de la map en haut à
// gauche, carrousel de cartes illustrées à droite, recherche, pied de page coloré.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/game.h"
#include "core/anim.h"
#include "core/fsutil.h"
#include "game/library.h"
#include "ui/theme.h"

namespace {

constexpr float kSetH = 128, kDiffH = 84, kGap = 10;
constexpr float kListX = 1000;

class SongSelect : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        enterTime_ = game.ctx.now;
        set_ = std::max(0, game.selectedSet);
        diff_ = std::max(0, game.selectedDiff);
        filter();
        scroll_ = targetScroll();
        wedgeTime_ = game.ctx.now;
        if (game.currentSet()) game.select(set_, diff_, true);
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        const auto &sets = c.library->sets();
        if (lastCount_ != sets.size()) filter();  // la bibliothèque a changé (scan, téléchargement)
        typing(in);

        if (modsOpen_) {
            updateMods(in);
            return;
        }
        if (in.back) {
            if (!query_.empty()) {
                query_.clear();
                filter();
                return;
            }
            c.play(c.sfx->uiBack);
            game.goTo(game.makeMainMenu());
            return;
        }
        // Pied de page (souris).
        std::vector<float> widths;
        const std::vector<float> xs = footer(widths);
        for (size_t i = 0; i < xs.size(); ++i) {
            footerHover_[i] = in.hover(xs[i], kVH - ui::kFooterH, widths[i], ui::kFooterH);
            if (in.clicked(xs[i], kVH - ui::kFooterH, widths[i], ui::kFooterH)) footerAction(static_cast<int>(i));
        }
        if (visible_.empty()) {
            if (in.confirm && sets.empty()) {
                c.play(c.sfx->uiSelect);
                game.goTo(game.makeDownload());
            }
            return;
        }
        if (in.extra2) footerAction(1);
        if (in.options) footerAction(3);

        const int oldSet = set_, oldDiff = diff_;
        int pos = visibleIndex();
        const int nDiffs = static_cast<int>(sets[static_cast<size_t>(set_)].diffs.size());
        if (in.down || in.wheel < 0) {
            if (diff_ + 1 < nDiffs && !(in.wheel < 0)) {
                ++diff_;
            } else if (pos + 1 < static_cast<int>(visible_.size())) {
                set_ = visible_[static_cast<size_t>(pos + 1)];
                diff_ = 0;
            }
        }
        if (in.up || in.wheel > 0) {
            if (diff_ > 0 && !(in.wheel > 0)) {
                --diff_;
            } else if (pos > 0) {
                set_ = visible_[static_cast<size_t>(pos - 1)];
                diff_ = in.wheel > 0 ? 0 : static_cast<int>(sets[static_cast<size_t>(set_)].diffs.size()) - 1;
            }
        }
        pos = visibleIndex();
        if (in.right || in.r1) {
            set_ = visible_[static_cast<size_t>(std::min(static_cast<int>(visible_.size()) - 1, pos + (in.r1 ? 10 : 1)))];
            diff_ = 0;
        }
        if (in.left || in.l1) {
            set_ = visible_[static_cast<size_t>(std::max(0, pos - (in.l1 ? 10 : 1)))];
            diff_ = 0;
        }
        if (in.extra) footerAction(2);

        // Clics dans le carrousel.
        bool playClicked = false;
        for (const Hit &h : hits_) {
            if (!in.clicked(h.x, h.y, h.w, h.h)) continue;
            if (h.set == set_ && h.diff == diff_ && h.diff >= 0) {
                playClicked = true;
            } else {
                set_ = h.set;
                diff_ = h.diff >= 0 ? h.diff : 0;
            }
            break;
        }
        if (set_ != oldSet || diff_ != oldDiff) {
            c.play(c.sfx->uiHover, 0.7f);
            changeTime_ = c.now;
            pending_ = true;
            if (set_ != oldSet) wedgeTime_ = c.now;
        }
        if (pending_ && c.now - changeTime_ > 0.15) {
            pending_ = false;
            game.select(set_, diff_, true);
        }
        if (in.confirm || playClicked) footerAction(4);
        scroll_ = anim::approach(scroll_, targetScroll(), c.dt, 0.05f);
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        hits_.clear();
        game.drawMenuBackground(0.5f);
        const float in = anim::outQuint(static_cast<float>((c.now - enterTime_) / 0.6));
        const auto &sets = c.library->sets();

        renderSearch(in);
        if (visible_.empty()) {
            const char *msg = c.library->scanning() ? "Lecture de la bibliothèque…"
                              : sets.empty()        ? "Aucune map installée"
                                                    : "Aucune map ne correspond à la recherche";
            c.fonts->bold.drawShadow(b, kVW / 2, kVH / 2 - 80, 52, ui::color::text, msg, gfx::Align::Center);
            if (sets.empty() && !c.library->scanning())
                c.fonts->semibold.drawShadow(b, kVW / 2, kVH / 2, 30, ui::color::textDim,
                                             "Croix : télécharger des maps directement depuis l'appli", gfx::Align::Center);
            renderFooter();
            return;
        }

        // --- Carrousel ------------------------------------------------------------------
        float y = kVH * 0.45f - scroll_;
        for (int vi = 0; vi < static_cast<int>(visible_.size()); ++vi) {
            const int s = visible_[static_cast<size_t>(vi)];
            const game::SetInfo &set = sets[static_cast<size_t>(s)];
            const bool open = s == set_;
            const float h = open ? kSetH + static_cast<float>(set.diffs.size()) * (kDiffH + 8) + 8 : kSetH;
            if (y + h > 60 && y < kVH - 40) renderSet(s, set, open, y, in);
            y += h + kGap;
        }

        renderWedge(in);
        renderScores(in);
        renderFooter();
        if (modsOpen_) renderMods();
    }

private:
    struct Hit {
        float x, y, w, h;
        int set, diff;
    };

    // --- Données -------------------------------------------------------------------------

    void filter() {
        const auto &sets = game.ctx.library->sets();
        lastCount_ = sets.size();
        visible_.clear();
        const std::string q = fsu::toLower(query_);
        for (int i = 0; i < static_cast<int>(sets.size()); ++i) {
            const game::SetInfo &s = sets[static_cast<size_t>(i)];
            if (!q.empty()) {
                std::string hay = fsu::toLower(s.title + " " + s.artist + " " + s.creator + " " + s.tags + " " + s.source);
                for (const game::DiffInfo &d : s.diffs) hay += " " + fsu::toLower(d.version);
                if (hay.find(q) == std::string::npos) continue;
            }
            visible_.push_back(i);
        }
        if (!visible_.empty() && std::find(visible_.begin(), visible_.end(), set_) == visible_.end()) {
            set_ = visible_[0];
            diff_ = 0;
            pending_ = true;
            changeTime_ = game.ctx.now;
        }
        if (!sets.empty() && set_ < static_cast<int>(sets.size()))
            diff_ = std::clamp(diff_, 0, static_cast<int>(sets[static_cast<size_t>(set_)].diffs.size()) - 1);
    }

    int visibleIndex() const {
        auto it = std::find(visible_.begin(), visible_.end(), set_);
        return it == visible_.end() ? 0 : static_cast<int>(it - visible_.begin());
    }

    float targetScroll() const {
        return static_cast<float>(visibleIndex()) * (kSetH + kGap) + kSetH + static_cast<float>(diff_) * (kDiffH + 8) - kDiffH;
    }

    void typing(const UiInput &in) {
        if (!in.raw || !in.raw->keyboard.connected || modsOpen_) return;
        const bool *k = in.raw->keyboard.keys;
        const bool shift = k[0xE1] || k[0xE5];
        bool changed = false;
        for (int code = 0x04; code <= 0x2C; ++code) {
            if (!k[code] || prevKeys_[code]) continue;
            // Z et X restent des touches de jeu : on ne les capte pas tant que la recherche est vide.
            char ch = 0;
            if (code <= 0x1D) ch = static_cast<char>((shift ? 'A' : 'a') + (code - 0x04));
            else if (code <= 0x26) ch = static_cast<char>('1' + (code - 0x1E));
            else if (code == 0x27) ch = '0';
            else if (code == 0x2C && !query_.empty()) ch = ' ';
            else if (code == 0x2A && !query_.empty()) {
                query_.pop_back();
                changed = true;
            }
            if (ch && query_.size() < 50) {
                query_ += ch;
                changed = true;
            }
        }
        for (int i = 0; i < 256; ++i) prevKeys_[i] = k[i];
        if (changed) filter();
    }

    std::vector<float> footer(std::vector<float> &widths) {
        return ui::footerLayout(game.ctx, {"Retour", "Mods", "Au hasard", "Options", "Jouer"}, 0, widths);
    }

    void footerAction(int i) {
        Context &c = game.ctx;
        const auto &sets = c.library->sets();
        switch (i) {
        case 0:
            c.play(c.sfx->uiBack);
            game.goTo(game.makeMainMenu());
            break;
        case 1:
            modsOpen_ = true;
            c.play(c.sfx->uiSelect);
            break;
        case 2:
            if (!visible_.empty()) {
                set_ = visible_[static_cast<size_t>(rand() % static_cast<int>(visible_.size()))];
                diff_ = 0;
                wedgeTime_ = c.now;
                changeTime_ = c.now;
                pending_ = true;
                c.play(c.sfx->uiOpen, 0.6f);
            }
            break;
        case 3: game.goTo(game.makeOptions()); break;
        default:
            if (!visible_.empty() && set_ < static_cast<int>(sets.size())) {
                game.select(set_, diff_, false);
                c.play(c.sfx->uiSelect);
                c.saveSettings();
                game.goTo(game.makePlay());
            }
            break;
        }
    }

    // --- Rendu ------------------------------------------------------------------------------

    void renderSet(int s, const game::SetInfo &set, bool open, float y, float in) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float centre = std::fabs(y + kSetH / 2 - kVH * 0.45f) / kVH;
        const float x = kListX + centre * centre * 220 - (open ? 50 : 0) + (1 - in) * 600;
        const float w = kVW - x + 40;
        const uint32_t edge = open ? ui::color::pink : gfx::rgba(255, 255, 255, 30);
        ui::roundRect(c, x - 3, y - 3, w + 6, kSetH + 6, 20, edge);
        ui::roundRect(c, x, y, w, kSetH, 18, gfx::rgba(30, 26, 38));
        if (const gfx::Texture *th = game.thumbnail(s)) {
            // Image recadrée à la proportion de la carte.
            const float aspect = w / kSetH, ta = static_cast<float>(th->width) / static_cast<float>(th->height);
            float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
            if (ta > aspect) {
                const float cut = (1 - aspect / ta) / 2;
                u0 = cut;
                u1 = 1 - cut;
            } else {
                const float cut = (1 - ta / aspect) / 2;
                v0 = cut;
                v1 = 1 - cut;
            }
            b.quad(*th, x + 4, y + 4, w - 8, kSetH - 8, gfx::rgba(255, 255, 255, open ? 255 : 200), u0, v0, u1, v1);
        }
        // Dégradé sombre à gauche pour la lisibilité du texte (comme lazer).
        b.gradient(x + 4, y + 4, w - 8, kSetH - 8, gfx::rgba(0, 0, 0, 60), gfx::rgba(0, 0, 0, 150));
        for (int k = 0; k < 10; ++k)
            b.rect(x + 4 + static_cast<float>(k) * 60, y + 4, 60, kSetH - 8, gfx::rgba(0, 0, 0, static_cast<uint8_t>(150 - k * 15)));
        const std::string title = c.fonts->bold.ellipsize(36, set.title, w - 80);
        c.fonts->bold.drawShadow(b, x + 28, y + 16, 36, ui::color::text, title.c_str());
        char line[300];
        snprintf(line, sizeof(line), "%s  ·  %s", set.artist.c_str(), set.creator.c_str());
        const std::string sub = c.fonts->semibold.ellipsize(24, line, w - 80);
        c.fonts->semibold.drawShadow(b, x + 30, y + 60, 24, gfx::rgba(230, 225, 240), sub.c_str());
        for (size_t d = 0; d < set.diffs.size() && d < 16; ++d) {
            const float dx = x + 40 + static_cast<float>(d) * 26;
            b.centered(c.tex->disc, dx, y + 104, 20, gfx::rgba(0, 0, 0, 150));
            b.centered(c.tex->ringThick, dx, y + 104, 18, ui::starColor(set.diffs[d].stars));
        }
        hits_.push_back({x, y, w, kSetH, s, -1});
        if (!open) return;
        float dy = y + kSetH + 8;
        for (int d = 0; d < static_cast<int>(set.diffs.size()); ++d) {
            const game::DiffInfo &df = set.diffs[static_cast<size_t>(d)];
            const bool sel = d == diff_;
            const float dx = x + 50 - (sel ? 24 : 0), dw = kVW - dx + 40;
            const uint32_t sc = ui::starColor(df.stars);
            ui::roundRect(c, dx - 3, dy - 3, dw + 6, kDiffH + 6, 18, sel ? gfx::rgba(255, 255, 255) : gfx::rgba(255, 255, 255, 20));
            ui::roundRect(c, dx, dy, dw, kDiffH, 16, sel ? gfx::rgba(48, 40, 62) : gfx::rgba(36, 32, 46, 235));
            b.rect(dx, dy + 10, 8, kDiffH - 20, sc);
            b.centered(c.tex->ringThick, dx + 50, dy + kDiffH / 2, 44, sc);
            b.centered(c.tex->disc, dx + 50, dy + kDiffH / 2, 18, sc);
            const std::string ver = c.fonts->bold.ellipsize(28, df.version, dw - 340);
            c.fonts->bold.draw(b, dx + 90, dy + 12, 28, ui::color::text, ver.c_str());
            char stars[32];
            if (df.stars >= 0)
                snprintf(stars, sizeof(stars), "%.2f", df.stars);
            else
                snprintf(stars, sizeof(stars), "?");
            // Étoiles pleines comme dans lazer.
            const int full = df.stars > 0 ? std::min(10, static_cast<int>(df.stars)) : 0;
            for (int k = 0; k < 10; ++k)
                ui::icon(c, ui::Icon::Star, dx + 100 + static_cast<float>(k) * 26, dy + 60, 18,
                         k < full ? sc : gfx::rgba(255, 255, 255, 40));
            ui::roundRect(c, dx + dw - 190, dy + 22, 110, 40, 20, sc);
            c.fonts->bold.draw(b, dx + dw - 135, dy + 27, 26, df.stars > 6.5f || df.stars < 0 ? ui::color::text : gfx::rgba(20, 20, 30), stars,
                               gfx::Align::Center);
            hits_.push_back({dx, dy, dw, kDiffH, s, d});
            dy += kDiffH + 8;
        }
    }

    void renderWedge(float in) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *df = game.currentDiff();
        if (!set || !df || set_ != game.selectedSet) {
            set = &c.library->sets()[static_cast<size_t>(set_)];
            df = &set->diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(set->diffs.size()) - 1))];
        }
        const float k = anim::outQuint(static_cast<float>((c.now - wedgeTime_) / 0.5)) * in;
        const float x = -40 - (1 - k) * 200, y = 70, w = 980, h = 330;
        ui::skewed(c, x, y, w, h, 40, gfx::withAlpha(gfx::rgba(20, 16, 28), 0.95f * k));
        if (const gfx::Texture *th = game.thumbnail(set_)) {
            const float ta = static_cast<float>(th->width) / static_cast<float>(th->height), aspect = (w - 60) / h;
            float v0 = 0, v1 = 1;
            if (ta < aspect) {
                const float cut = (1 - ta / aspect) / 2;
                v0 = cut;
                v1 = 1 - cut;
            }
            b.quad(*th, x + 60, y + 6, w - 90, h - 12, gfx::withAlpha(gfx::rgba(255, 255, 255), k), 0, v0, 1, v1);
            b.gradient(x + 60, y + 6, w - 90, h - 12, gfx::rgba(0, 0, 0, 30), gfx::withAlpha(gfx::rgba(0, 0, 0), 0.85f * k));
        }
        b.rect(x + 60, y + h - 6, w - 90, 6, gfx::withAlpha(ui::starColor(df->stars), k));
        // Pastille d'étoiles et nom de difficulté.
        char line[256];
        const uint32_t sc = ui::starColor(df->stars);
        if (df->stars >= 0)
            snprintf(line, sizeof(line), "★ %.2f", df->stars);
        else
            snprintf(line, sizeof(line), "★ ?");
        ui::roundRect(c, 80, y + 22, 140, 44, 22, gfx::withAlpha(sc, k));
        c.fonts->bold.draw(b, 150, y + 28, 28, gfx::withAlpha(df->stars > 6.5f || df->stars < 0 ? ui::color::text : gfx::rgba(20, 20, 30), k),
                           line, gfx::Align::Center);
        const std::string ver = c.fonts->semibold.ellipsize(28, df->version, 640);
        c.fonts->semibold.drawShadow(b, 236, y + 28, 28, gfx::withAlpha(ui::color::text, k), ver.c_str());
        const std::string title = c.fonts->bold.ellipsize(52, set->title, 840);
        c.fonts->bold.drawShadow(b, 80, y + 150, 52, gfx::withAlpha(ui::color::text, k), title.c_str());
        snprintf(line, sizeof(line), "%s", set->artist.c_str());
        c.fonts->semibold.drawShadow(b, 82, y + 212, 32, gfx::withAlpha(gfx::rgba(235, 230, 245), k), line);
        snprintf(line, sizeof(line), "mappée par %s", set->creator.c_str());
        c.fonts->regular.drawShadow(b, 82, y + 254, 24, gfx::withAlpha(ui::color::textDim, k), line);

        // Rangée de statistiques avec icônes colorées.
        const float sy = y + h + 18;
        struct Stat {
            ui::Icon icon;
            uint32_t color;
            std::string text;
        };
        char bpm[32];
        snprintf(bpm, sizeof(bpm), "%.0f BPM", df->bpm);
        const Stat stats[] = {{ui::Icon::Clock, ui::color::yellow, ui::formatTime(df->lengthMs)},
                              {ui::Icon::Music, ui::color::pink, bpm},
                              {ui::Icon::Circles, ui::color::blue, std::to_string(df->circles)},
                              {ui::Icon::Sliders, ui::color::lime, std::to_string(df->sliders)}};
        float sx = 80;
        for (const Stat &st : stats) {
            ui::icon(c, st.icon, sx + 14, sy + 18, 28, gfx::withAlpha(st.color, k));
            sx += 40 + c.fonts->semibold.drawShadow(b, sx + 36, sy + 2, 28, gfx::withAlpha(ui::color::text, k), st.text.c_str()) + 30;
        }
        // Barres CS / AR / OD / HP.
        const char *names[] = {"CS", "AR", "OD", "HP"};
        const float values[] = {df->cs, df->ar, df->od, df->hp};
        const uint32_t cols[] = {ui::color::blue, ui::color::pink, ui::color::yellow, ui::color::lime};
        for (int i = 0; i < 4; ++i) {
            const float bx = 80 + static_cast<float>(i) * 220, by = sy + 56;
            c.fonts->bold.draw(b, bx, by, 24, gfx::withAlpha(ui::color::textDim, k), names[i]);
            ui::roundRect(c, bx + 50, by + 8, 120, 12, 6, gfx::withAlpha(gfx::rgba(255, 255, 255, 40), k));
            ui::roundRect(c, bx + 50, by + 8, 120 * std::clamp(values[i] / 10.0f, 0.05f, 1.0f), 12, 6, gfx::withAlpha(cols[i], k));
            snprintf(line, sizeof(line), "%.1f", values[i]);
            c.fonts->semibold.draw(b, bx + 180, by, 22, gfx::withAlpha(ui::color::text, k), line);
        }
        const std::string mods = game.mods.label();
        if (!mods.empty()) {
            snprintf(line, sizeof(line), "Mods : %s", mods.c_str());
            c.fonts->bold.drawShadow(b, 80, sy + 100, 28, gfx::withAlpha(ui::color::yellow, k), line);
        }
    }

    void renderScores(float in) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const game::SetInfo &set = c.library->sets()[static_cast<size_t>(set_)];
        const game::DiffInfo &df = set.diffs[static_cast<size_t>(std::clamp(diff_, 0, static_cast<int>(set.diffs.size()) - 1))];
        const std::vector<LocalScore> scores = game.scoresFor(Game::diffKey(set, df));
        const float x = 40 - (1 - in) * 500, y = 560;
        ui::roundRect(c, x, y, 880, 420, 22, gfx::rgba(20, 16, 28, 210));
        c.fonts->bold.draw(b, x + 28, y + 18, 28, ui::color::text, "Classement local");
        if (scores.empty()) c.fonts->regular.draw(b, x + 28, y + 70, 24, ui::color::textDim, "Pas encore de score sur cette difficulté.");
        char line[128];
        for (size_t i = 0; i < scores.size() && i < 5; ++i) {
            const LocalScore &s = scores[i];
            const float ry = y + 66 + static_cast<float>(i) * 68;
            ui::roundRect(c, x + 20, ry, 840, 60, 14, gfx::rgba(255, 255, 255, 14));
            uint32_t rc = ui::color::pink;
            if (s.rank[0] == 'S') rc = ui::color::yellow;
            else if (s.rank == "A") rc = ui::color::lime;
            else if (s.rank == "B") rc = ui::color::blue;
            c.fonts->bold.draw(b, x + 70, ry + 8, 38, rc, s.rank.c_str(), gfx::Align::Center);
            snprintf(line, sizeof(line), "%lld", s.score);
            c.fonts->bold.draw(b, x + 130, ry + 12, 30, ui::color::text, line);
            snprintf(line, sizeof(line), "%.2f%%  ·  %dx  %s", s.accuracy * 100, s.maxCombo, s.mods.c_str());
            c.fonts->semibold.draw(b, x + 840, ry + 16, 24, ui::color::textDim, line, gfx::Align::Right);
        }
    }

    void renderSearch(float in) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const float x = kListX + 40 + (1 - in) * 500, y = 20, w = kVW - x - 30;
        ui::roundRect(c, x, y, w, 62, 31, gfx::rgba(20, 16, 28, 220));
        ui::icon(c, ui::Icon::Search, x + 36, y + 31, 28, ui::color::pink);
        const bool empty = query_.empty();
        const std::string shown = empty ? "Tape pour chercher (clavier)…" : query_ + "_";
        c.fonts->semibold.draw(b, x + 70, y + 16, 26, empty ? ui::color::textDim : ui::color::text, shown.c_str());
        char line[64];
        snprintf(line, sizeof(line), "%zu / %zu", visible_.size(), c.library->sets().size());
        c.fonts->semibold.draw(b, x + w - 26, y + 16, 26, ui::color::textDim, line, gfx::Align::Right);
    }

    void renderFooter() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        b.rect(0, kVH - ui::kFooterH, kVW, ui::kFooterH, gfx::rgba(18, 16, 24, 235));
        std::vector<float> widths;
        const std::vector<float> xs = footer(widths);
        const char *labels[] = {"Retour", "Mods", "Au hasard", "Options", "Jouer"};
        const uint32_t cols[] = {ui::color::pink, ui::color::purple, gfx::rgba(70, 180, 110), gfx::rgba(52, 160, 230), ui::color::pink};
        const ui::PadIcon pads[] = {ui::PadIcon::Circle, ui::PadIcon::Square, ui::PadIcon::Triangle, ui::PadIcon::Options, ui::PadIcon::Cross};
        for (size_t i = 0; i < xs.size(); ++i) {
            float x = xs[i];
            if (i == 4) x = kVW - widths[i] - 20;  // "Jouer" à droite
            ui::footerButton(c, x, widths[i], labels[i], cols[i], pads[i], footerHover_[i]);
        }
    }

    void updateMods(const UiInput &in) {
        Context &c = game.ctx;
        if (in.back || in.extra2) {
            modsOpen_ = false;
            c.play(c.sfx->uiBack);
            return;
        }
        if (in.up) modCursor_ = (modCursor_ + 4) % 5;
        if (in.down) modCursor_ = (modCursor_ + 1) % 5;
        if (in.up || in.down) c.play(c.sfx->uiHover, 0.8f);
        bool toggle = in.confirm;
        for (int i = 0; i < 5; ++i) {
            const float ry = 360 + static_cast<float>(i) * 100;
            if (in.hover(kVW / 2 - 400, ry, 800, 88)) modCursor_ = i;
            if (in.clicked(kVW / 2 - 400, ry, 800, 88)) toggle = true;
        }
        if (in.mouseClick && !in.hover(kVW / 2 - 440, 240, 880, 660)) {
            modsOpen_ = false;
            return;
        }
        if (toggle) {
            game::Mods &m = game.mods;
            bool *flags[] = {&m.easy, &m.noFail, &m.hidden, &m.hardRock, &m.autoplay};
            *flags[modCursor_] = !*flags[modCursor_];
            if (modCursor_ == 0 && m.easy) m.hardRock = false;
            if (modCursor_ == 3 && m.hardRock) m.easy = false;
            c.play(c.sfx->uiSelect);
        }
    }

    void renderMods() {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        b.rect(0, 0, kVW, kVH, gfx::rgba(0, 0, 0, 160));
        const float x = kVW / 2 - 440, y = 240;
        ui::roundRect(c, x, y, 880, 660, 30, gfx::rgba(26, 22, 34, 250));
        ui::trianglePanel(c, x + 20, y + 20, 840, 90, ui::darken(ui::color::purple, 0.3f), 5);
        c.fonts->bold.drawShadow(b, x + 50, y + 38, 48, ui::color::text, "Mods");
        const game::Mods &m = game.mods;
        const bool on[] = {m.easy, m.noFail, m.hidden, m.hardRock, m.autoplay};
        const char *names[] = {"Easy", "No Fail", "Hidden", "Hard Rock", "Auto"};
        const char *codes[] = {"EZ", "NF", "HD", "HR", "AT"};
        const uint32_t cols[] = {ui::color::lime, ui::color::lime, ui::color::yellow, ui::color::red, ui::color::blue};
        const char *desc[] = {"Cercles plus gros, plus lents, plus tolérants", "Impossible d'échouer",
                              "Les cercles disparaissent avant d'être frappés", "Plus petits, plus rapides, plus stricts",
                              "Regarder la map se jouer toute seule"};
        for (int i = 0; i < 5; ++i) {
            const float ry = 360 + static_cast<float>(i) * 100;
            const bool sel = i == modCursor_;
            ui::roundRect(c, kVW / 2 - 400, ry, 800, 88, 20, sel ? gfx::rgba(255, 255, 255, 30) : gfx::rgba(255, 255, 255, 10));
            ui::skewed(c, kVW / 2 - 380, ry + 16, 100, 56, 10, on[i] ? cols[i] : gfx::rgba(255, 255, 255, 30));
            c.fonts->bold.draw(b, kVW / 2 - 325, ry + 24, 30, on[i] ? gfx::rgba(20, 20, 30) : ui::color::textDim, codes[i], gfx::Align::Center);
            c.fonts->bold.draw(b, kVW / 2 - 250, ry + 12, 32, ui::color::text, names[i]);
            c.fonts->regular.draw(b, kVW / 2 - 250, ry + 50, 22, ui::color::textDim, desc[i]);
        }
    }

    int set_ = 0, diff_ = 0;
    float scroll_ = 0;
    double enterTime_ = 0, changeTime_ = 0, wedgeTime_ = 0;
    bool pending_ = false, modsOpen_ = false;
    int modCursor_ = 0;
    std::vector<int> visible_;
    size_t lastCount_ = 0;
    std::string query_;
    bool prevKeys_[256] = {};
    bool footerHover_[5] = {};
    std::vector<Hit> hits_;
};

}  // namespace

std::unique_ptr<Screen> Game::makeSongSelect() { return std::make_unique<SongSelect>(*this); }
