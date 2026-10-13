// SPDX-License-Identifier: GPL-3.0-or-later
// Écran de jeu : charge la map, fait tourner game::Play, gère la pause et la fin.
#include <time.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "app/game.h"
#include "core/anim.h"
#include "core/fsutil.h"
#include "core/log.h"
#include "game/library.h"
#include "render/image.h"
#include "ui/theme.h"

namespace {

class PlayScreen : public Screen {
public:
    using Screen::Screen;
    bool gameplay() const override { return !play_.paused(); }

    void enter() override {
        Context &c = game.ctx;
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        if (!set || !diff) {
            error_ = "Aucune map sélectionnée";
            return;
        }
        const std::string folder = set->path(c.songsDir);
        std::string text;
        game::Beatmap map;
        if (!fsu::readText(folder + "/" + diff->file, text) || !game::parseBeatmap(text, map, false)) {
            error_ = "Impossible de lire la map";
            return;
        }
        map.folder = folder;
        map.file = diff->file;
        key_ = Game::diffKey(*set, *diff);
        // Fond net (synchrone : on est pendant le fondu au noir).
        if (!map.background.empty()) {
            gfx::ImageData img;
            if (gfx::decodeImageFile(folder + "/" + map.background, 1920, img))
                background_ = gfx::createTexture(img.width, img.height, img.rgba.data(), true);
        }
        if (!c.music->load(folder + "/" + map.audioFile)) {
            error_ = "Musique introuvable : " + map.audioFile;
            return;
        }
        if (!play_.start(c, std::move(map), game.mods, &background_)) error_ = "Map vide";
        started_ = true;
    }

    void leave() override {
        play_.stop(game.ctx);
        gfx::destroyTexture(background_);
        // L'aperçu reprendra dans les menus.
        game.ctx.music->setVolume(game.ctx.settings->musicVolume);
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        if (!error_.empty()) {
            if (in.back || in.confirm) game.goTo(game.makeSongSelect());
            return;
        }
        if (play_.paused()) {
            if (in.up) pauseCursor_ = (pauseCursor_ + 2) % 3;
            if (in.down) pauseCursor_ = (pauseCursor_ + 1) % 3;
            if (in.up || in.down) c.play(c.sfx->uiHover, 0.8f);
            bool activate = in.confirm;
            for (int i = 0; i < 3; ++i) {
                const float y = 420 + static_cast<float>(i) * 120;
                if (in.hover(kVW / 2 - 330, y, 660, 96) && pauseCursor_ != i) {
                    pauseCursor_ = i;
                    c.play(c.sfx->uiHover, 0.7f);
                }
                if (in.clicked(kVW / 2 - 330, y, 660, 96)) activate = true;
            }
            if (in.options || in.back) {
                play_.setPaused(c, false);
                return;
            }
            if (activate) {
                c.play(c.sfx->uiSelect);
                if (pauseCursor_ == 0) {
                    play_.setPaused(c, false);
                } else if (pauseCursor_ == 1) {
                    game.goTo(game.makePlay());
                } else {
                    game.goTo(game.makeSongSelect());
                }
            }
            return;
        }
        if (in.options || in.back) {
            play_.setPaused(c, true);
            pauseCursor_ = 0;
            pauseTime_ = c.now;
            c.play(c.sfx->uiOpen, 0.7f);
            return;
        }
        if (in.extra && play_.canSkip()) play_.skip(c);
        play_.update(c, in);
        if (play_.finished() && !leaving_) {
            leaving_ = true;
            const game::PlayResult &r = play_.result();
            if (r.completed && !r.mods.autoplay) {
                LocalScore s;
                s.key = key_;
                s.score = r.score;
                s.accuracy = r.accuracy;
                s.maxCombo = r.maxCombo;
                s.n300 = r.n300;
                s.n100 = r.n100;
                s.n50 = r.n50;
                s.nMiss = r.nMiss;
                s.rank = r.rank;
                s.mods = r.mods.label();
                s.date = static_cast<long long>(time(nullptr));
                game.addScore(s);
            }
            game.goTo(game.makeResults(r));
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        if (!error_.empty()) {
            game.drawMenuBackground(0.7f);
            c.fonts->bold.drawShadow(b, kVW / 2, kVH / 2 - 60, 48, ui::color::bad, error_.c_str(), gfx::Align::Center);
            ui::hintBar(c, {{ui::PadIcon::Circle, "Retour"}});
            return;
        }
        if (!started_) return;
        play_.render(c);
        if (play_.paused()) {
            const float k = anim::outCubic(static_cast<float>((c.now - pauseTime_) / 0.25));
            b.rect(0, 0, kVW, kVH, gfx::withAlpha(gfx::rgba(8, 6, 14), 0.75f * k));
            c.fonts->bold.drawShadow(b, kVW / 2, 230, 96, gfx::withAlpha(ui::color::yellow, k), "pause", gfx::Align::Center);
            c.fonts->semibold.drawShadow(b, kVW / 2, 345, 28, gfx::withAlpha(ui::color::textDim, k), "Tu veux continuer ?", gfx::Align::Center);
            const char *items[] = {"Continuer", "Recommencer", "Quitter"};
            const uint32_t cols[] = {gfx::rgba(70, 190, 100), ui::color::yellow, gfx::rgba(235, 74, 74)};
            for (int i = 0; i < 3; ++i) {
                const bool sel = i == pauseCursor_;
                const float y = 420 + static_cast<float>(i) * 120;
                const float grow = sel ? 20.0f : 0.0f;
                ui::roundRect(c, kVW / 2 - 300 - grow, y, 600 + grow * 2, 96, 48,
                              gfx::withAlpha(sel ? cols[i] : ui::darken(cols[i], 0.55f), k));
                if (sel) ui::trianglePanel(c, kVW / 2 - 240, y + 8, 480, 80, cols[i], 30 + i, 0.5f * k);
                c.fonts->bold.drawShadow(b, kVW / 2, y + 22, 44, gfx::withAlpha(ui::color::text, k), items[i], gfx::Align::Center);
            }
            ui::hintBar(c, {{ui::PadIcon::Cross, "Valider"}, {ui::PadIcon::Options, "Reprendre"}}, k);
        }
    }

private:
    game::Play play_;
    gfx::Texture background_;
    std::string error_, key_;
    bool started_ = false, leaving_ = false;
    int pauseCursor_ = 0;
    double pauseTime_ = 0;
};

class Results : public Screen {
public:
    Results(Game &g, const game::PlayResult &r) : Screen(g), r_(r) {}

    void enter() override {
        enterTime_ = game.ctx.now;
        game.ctx.play(r_.failed ? game.ctx.sfx->uiError : game.ctx.sfx->uiDownloaded);
        game.playPreview();
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        std::vector<float> widths;
        const std::vector<float> xs = ui::footerLayout(c, {"Retour", "Rejouer", "Corriger le décalage"}, 0, widths);
        int clicked = -1;
        for (size_t i = 0; i < xs.size(); ++i) {
            hover_[i] = in.hover(xs[i], kVH - ui::kFooterH, widths[i], ui::kFooterH);
            if (in.clicked(xs[i], kVH - ui::kFooterH, widths[i], ui::kFooterH)) clicked = static_cast<int>(i);
        }
        if (in.back || in.confirm || clicked == 0) {
            c.play(c.sfx->uiBack);
            game.goTo(game.makeSongSelect());
        }
        if (in.extra2 || clicked == 1) {
            c.play(c.sfx->uiSelect);
            game.goTo(game.makePlay());
        }
        if ((in.extra || clicked == 2) && canSuggest()) {
            c.settings->offsetMs += static_cast<int>(std::lround(r_.meanError));
            c.saveSettings();
            offsetApplied_ = true;
            c.play(c.sfx->uiSelect);
            game.toast("Décalage audio réglé à " + std::to_string(c.settings->offsetMs) + " ms");
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        game.drawMenuBackground(0.6f);
        const float t = static_cast<float>(c.now - enterTime_);
        const game::SetInfo *set = game.currentSet();
        const game::DiffInfo *diff = game.currentDiff();
        char line[256];
        ui::header(c, "Résultats", nullptr, ui::color::pink, anim::outQuint(t / 0.5f));
        if (set && diff) {
            snprintf(line, sizeof(line), "%s - %s [%s]", set->artist.c_str(), set->title.c_str(), diff->version.c_str());
            const std::string sub = c.fonts->semibold.ellipsize(28, line, 1300);
            c.fonts->semibold.drawShadow(b, 82, 112, 28, ui::lighten(ui::color::pink, 0.6f), sub.c_str());
        }

        // Cercle de précision : se remplit jusqu'à la précision obtenue.
        const float cx = 520, cy = 590, R = 260;
        const float fill = anim::outCubic(std::max(0.0f, (t - 0.3f) / 1.4f)) * static_cast<float>(r_.accuracy);
        b.centered(c.tex->ringThick, cx, cy, R * 2, gfx::rgba(255, 255, 255, 30));
        const int dots = 360;
        for (int i = 0; i < dots; ++i) {
            const float f = static_cast<float>(i) / dots;
            if (f >= fill) break;
            const float a = -1.5708f + f * 6.2831853f;
            // Couleurs des paliers de rang autour du cercle (D rouge → SS doré).
            uint32_t col = ui::color::red;
            if (f > 0.97f) col = gfx::rgba(255, 220, 110);
            else if (f > 0.9f) col = ui::color::yellow;
            else if (f > 0.8f) col = ui::color::lime;
            else if (f > 0.7f) col = ui::color::blue;
            else if (f > 0.6f) col = ui::color::purple;
            b.centered(c.tex->disc, cx + std::cos(a) * (R - 15), cy + std::sin(a) * (R - 15), 30, col);
        }
        const float rk = anim::outBack(std::max(0.0f, (t - 1.4f) / 0.5f));
        uint32_t rc = ui::color::pink;
        if (r_.rank[0] == 'S') rc = gfx::rgba(255, 220, 110);
        else if (r_.rank == "A") rc = ui::color::lime;
        else if (r_.rank == "B") rc = ui::color::blue;
        else if (r_.rank == "C") rc = ui::color::purple;
        else if (r_.rank == "D" || r_.rank == "F") rc = ui::color::red;
        if (rk > 0) {
            b.setBlend(gfx::Blend::Additive);
            b.centered(c.tex->glow, cx, cy, R * 2.4f * rk, gfx::withAlpha(rc, 0.4f));
            b.setBlend(gfx::Blend::Alpha);
            c.fonts->bold.drawShadow(b, cx, cy - 130 * rk, 220 * rk, rc, r_.rank.c_str(), gfx::Align::Center);
        }
        snprintf(line, sizeof(line), "%.2f%%", r_.accuracy * 100 * std::min(1.0f, t / 1.7f));
        c.fonts->bold.drawShadow(b, cx, cy + 110, 44, ui::color::text, line, gfx::Align::Center);
        if (r_.failed) c.fonts->bold.drawShadow(b, cx, cy + 170, 36, ui::color::red, "Échec", gfx::Align::Center);

        // Score et statistiques.
        const float px = 900, py = 220;
        const float count = anim::outCubic((t - 0.2f) / 1.4f);
        ui::roundRect(c, px, py, 960, 700, 30, gfx::rgba(20, 16, 28, 225));
        snprintf(line, sizeof(line), "%08lld", static_cast<long long>(static_cast<double>(r_.score) * count));
        c.fonts->bold.drawShadow(b, px + 480, py + 30, 104, ui::color::text, line, gfx::Align::Center);
        if (!r_.mods.label().empty()) {
            snprintf(line, sizeof(line), "Mods : %s", r_.mods.label().c_str());
            c.fonts->bold.draw(b, px + 480, py + 150, 28, ui::color::yellow, line, gfx::Align::Center);
        }
        struct Tile {
            const char *label;
            int value;
            uint32_t color;
        };
        const Tile tiles[] = {{"Great", r_.n300, ui::color::blue},
                              {"Ok", r_.n100, ui::color::lime},
                              {"Meh", r_.n50, ui::color::yellow},
                              {"Miss", r_.nMiss, ui::color::red}};
        for (int i = 0; i < 4; ++i) {
            const float tx = px + 40 + static_cast<float>(i) * 225, ty = py + 210;
            const float pop = anim::outBack(std::max(0.0f, (t - 0.4f - 0.1f * i) / 0.4f));
            ui::roundRect(c, tx, ty + (1 - pop) * 40, 205, 150, 22, gfx::withAlpha(ui::darken(tiles[i].color, 0.55f), std::min(1.0f, pop)));
            b.rect(tx + 20, ty + (1 - pop) * 40, 165, 6, gfx::withAlpha(tiles[i].color, std::min(1.0f, pop)));
            c.fonts->bold.draw(b, tx + 102, ty + 24 + (1 - pop) * 40, 28, gfx::withAlpha(tiles[i].color, std::min(1.0f, pop)), tiles[i].label,
                               gfx::Align::Center);
            snprintf(line, sizeof(line), "%d", static_cast<int>(tiles[i].value * count));
            c.fonts->bold.draw(b, tx + 102, ty + 66 + (1 - pop) * 40, 54, gfx::withAlpha(ui::color::text, std::min(1.0f, pop)), line,
                               gfx::Align::Center);
        }
        const char *names[] = {"Combo max", "Précision", "Taux d'instabilité", "Écart moyen"};
        char values[4][48];
        snprintf(values[0], 48, "%dx", static_cast<int>(r_.maxCombo * count));
        snprintf(values[1], 48, "%.2f%%", r_.accuracy * 100);
        snprintf(values[2], 48, "%.1f UR", r_.unstableRate);
        snprintf(values[3], 48, "%+.1f ms", r_.meanError);
        for (int i = 0; i < 4; ++i) {
            const float y = py + 420 + static_cast<float>(i) * 60;
            c.fonts->semibold.draw(b, px + 60, y, 30, ui::color::textDim, names[i]);
            c.fonts->bold.draw(b, px + 900, y - 2, 34, ui::color::text, values[i], gfx::Align::Right);
        }
        if (canSuggest()) {
            snprintf(line, sizeof(line), "Tu frappes en moyenne %.0f ms %s.", std::fabs(r_.meanError),
                     r_.meanError > 0 ? "en retard" : "en avance");
            c.fonts->bold.drawShadow(b, kVW / 2, kVH - 130, 30, ui::color::yellow, line, gfx::Align::Center);
        }
        b.rect(0, kVH - ui::kFooterH, kVW, ui::kFooterH, gfx::rgba(18, 16, 24, 235));
        std::vector<float> widths;
        const std::vector<float> xs = ui::footerLayout(c, {"Retour", "Rejouer", "Corriger le décalage"}, 0, widths);
        const char *labels[] = {"Retour", "Rejouer", "Corriger le décalage"};
        const uint32_t cols[] = {ui::color::pink, ui::color::purple, ui::color::yellow};
        const ui::PadIcon pads[] = {ui::PadIcon::Circle, ui::PadIcon::Square, ui::PadIcon::Triangle};
        for (size_t i = 0; i < xs.size(); ++i) {
            if (i == 2 && !canSuggest()) continue;
            ui::footerButton(c, xs[i], widths[i], labels[i], cols[i], pads[i], hover_[i]);
        }
    }

private:
    bool canSuggest() const { return std::fabs(r_.meanError) > 8 && r_.n300 + r_.n100 + r_.n50 > 30 && !offsetApplied_ && !r_.mods.autoplay; }

    game::PlayResult r_;
    double enterTime_ = 0;
    bool offsetApplied_ = false;
    bool hover_[3] = {};
};

}  // namespace

std::unique_ptr<Screen> Game::makePlay() { return std::make_unique<PlayScreen>(*this); }
std::unique_ptr<Screen> Game::makeResults(const game::PlayResult &r) { return std::make_unique<Results>(*this, r); }
