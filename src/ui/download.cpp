// SPDX-License-Identifier: GPL-3.0-or-later
// Téléchargement de maps façon "beatmap listing" de lazer : bandeau bleu, onglets,
// grille de cartes illustrées. Croix (ou clic) : la map se télécharge et s'installe.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>

#include "app/game.h"
#include "core/anim.h"
#include "core/log.h"
#include "game/library.h"
#include "ui/theme.h"

namespace {

constexpr float kCardW = 880, kCardH = 168, kGapX = 40, kGapY = 22, kTop = 300;
constexpr uint32_t kBlue = gfx::rgba(52, 140, 230);

class Download : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        enterTime_ = game.ctx.now;
        refresh();
    }

    void leave() override {
        for (auto &kv : covers_) gfx::destroyTexture(kv.second);
        covers_.clear();
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        std::string error;
        std::vector<net::RemoteSet> results;
        if (game.mirror.takeResults(results, error)) {
            results_ = std::move(results);
            error_ = error;
            cursor_ = 0;
            scroll_ = 0;
            for (const net::RemoteSet &s : results_)
                if (!s.coverUrl.empty() && !covers_.count(s.id)) game.mirror.requestCover(s.id, s.coverUrl);
        }
        int coverId = 0;
        gfx::ImageData img;
        while (game.mirror.takeCover(coverId, img)) covers_[coverId] = gfx::createTexture(img.width, img.height, img.rgba.data(), true);

        // Téléchargements terminés : ajout à la bibliothèque (thread principal).
        for (const auto &d : game.mirror.downloads()) {
            if (handled_.count(d.get())) continue;
            if (d->phase.load() == 3) {
                handled_.insert(d.get());
                if (c.library->addFolder(d->folder)) {
                    c.play(c.sfx->uiDownloaded);
                    game.toast("Installée : " + d->label);
                    const int idx = c.library->findSet(d->folder);
                    if (idx >= 0 && game.selectedSet < 0) game.select(idx, 0, false);
                }
            } else if (d->phase.load() == 4) {
                handled_.insert(d.get());
                c.play(c.sfx->uiError);
                game.toast("Échec : " + d->error);
            }
        }

        if (in.back) {
            if (!query_.empty()) {
                query_.clear();
                refresh();
                return;
            }
            c.play(c.sfx->uiBack);
            game.goTo(game.makeMainMenu());
            return;
        }
        // Onglets (souris ou L1/R1).
        for (int i = 0; i < 3; ++i) {
            if (in.clicked(tabX(i), 196, tabW(i), 52) && tab_ != i) {
                tab_ = i;
                page_ = 0;
                c.play(c.sfx->uiHover);
                refresh();
            }
        }
        if (in.r1 || in.l1) {
            tab_ = (tab_ + (in.r1 ? 1 : 2)) % 3;
            page_ = 0;
            c.play(c.sfx->uiHover);
            refresh();
        }
        if (in.r2 || in.l2) {
            page_ = std::max(0, page_ + (in.r2 ? 1 : -1));
            c.play(c.sfx->uiHover);
            refresh();
        }
        if (in.extra) refresh();
        typing(in);

        const int n = static_cast<int>(results_.size());
        if (n > 0) {
            const int old = cursor_;
            if (in.right && cursor_ % 2 == 0 && cursor_ + 1 < n) ++cursor_;
            if (in.left && cursor_ % 2 == 1) --cursor_;
            if (in.down) cursor_ = std::min(n - 1, cursor_ + 2);
            if (in.up) cursor_ = std::max(0, cursor_ - 2);
            if (in.wheel != 0) scroll_ = std::clamp(scroll_ - in.wheel * 120, 0.0f, maxScroll());
            bool activate = in.confirm;
            for (int i = 0; i < n; ++i) {
                float x, y;
                cardPos(i, x, y);
                if (in.hover(x, y, kCardW, kCardH) && in.mouseMoved) cursor_ = i;
                if (in.clicked(x, y, kCardW, kCardH)) {
                    cursor_ = i;
                    activate = true;
                }
            }
            if (cursor_ != old) {
                c.play(c.sfx->uiHover, 0.6f);
                if (!in.mouseActive) {
                    // Garder la carte choisie visible.
                    const float row = static_cast<float>(cursor_ / 2) * (kCardH + kGapY);
                    if (row - scroll_ > 520) scroll_ = row - 520;
                    if (row < scroll_) scroll_ = row;
                }
            }
            if (activate) {
                const net::RemoteSet &s = results_[static_cast<size_t>(cursor_)];
                if (c.library->hasSetId(s.id)) {
                    game.toast("Déjà installée");
                } else {
                    game.mirror.download(s);
                    c.play(c.sfx->uiSelect);
                }
            }
        }
        smooth_ = anim::approach(smooth_, scroll_, c.dt, 0.05f);
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        game.drawMenuBackground(0.8f);
        const float in = anim::outQuint(static_cast<float>((c.now - enterTime_) / 0.6));
        char line[256];

        // Grille.
        const auto downloads = game.mirror.downloads();
        std::unordered_map<int, const net::DownloadState *> byId;
        for (const auto &d : downloads) byId[d->setId] = d.get();
        for (int i = 0; i < static_cast<int>(results_.size()); ++i) {
            float x, y;
            cardPos(i, x, y);
            if (y < 170 || y > kVH - 60) continue;
            renderCard(i, x, y, byId, in);
        }
        if (game.mirror.searchBusy()) {
            const float a = static_cast<float>(c.now) * 6;
            for (int k = 0; k < 8; ++k)
                b.centered(c.tex->disc, kVW / 2 + std::cos(a + k * 0.785f) * 40, 600 + std::sin(a + k * 0.785f) * 40, 16,
                           gfx::withAlpha(ui::color::blue, (k + 1) / 8.0f));
        } else if (!error_.empty()) {
            c.fonts->bold.drawShadow(b, kVW / 2, 540, 34, ui::color::red, ("Erreur réseau : " + error_).c_str(), gfx::Align::Center);
            c.fonts->semibold.drawShadow(b, kVW / 2, 590, 26, ui::color::textDim, "Triangle (ou F2) pour réessayer", gfx::Align::Center);
        } else if (results_.empty()) {
            c.fonts->bold.drawShadow(b, kVW / 2, 560, 34, ui::color::textDim, "Aucun résultat", gfx::Align::Center);
        }

        // En-tête, recherche et onglets par-dessus la grille.
        ui::header(c, "Télécharger des maps", "Depuis le miroir osu.direct · elles s'installent toutes seules", kBlue, in);
        b.rect(0, 170, kVW, 100, gfx::rgba(18, 18, 30, 240));
        for (int i = 0; i < 3; ++i) {
            const char *tabs[] = {"Populaires", "Récentes", "Favorites"};
            const bool sel = i == tab_;
            ui::roundRect(c, tabX(i), 196, tabW(i), 52, 26, sel ? kBlue : gfx::rgba(255, 255, 255, 18));
            c.fonts->bold.draw(b, tabX(i) + tabW(i) / 2, 206, 28, ui::color::text, tabs[i], gfx::Align::Center);
        }
        snprintf(line, sizeof(line), "Page %d", page_ + 1);
        c.fonts->semibold.draw(b, tabX(2) + tabW(2) + 30, 206, 28, ui::color::textDim, line);
        const float sx = 1080, sw = kVW - sx - 60;
        ui::roundRect(c, sx, 194, sw, 56, 28, gfx::rgba(0, 0, 0, 120));
        ui::icon(c, ui::Icon::Search, sx + 34, 222, 26, ui::color::blue);
        const bool empty = query_.empty();
        const std::string shown = empty ? "Rechercher (clavier)…" : query_ + "_";
        c.fonts->semibold.draw(b, sx + 66, 207, 26, empty ? ui::color::textDim : ui::color::text, shown.c_str());

        ui::hintBar(c,
                    {{ui::PadIcon::Cross, "Télécharger"},
                     {ui::PadIcon::L1, "/ R1 : onglet"},
                     {ui::PadIcon::L2, "/ R2 : page"},
                     {ui::PadIcon::Triangle, "Actualiser"},
                     {ui::PadIcon::Circle, "Retour"}},
                    in);
    }

private:
    float tabW(int i) const {
        const char *tabs[] = {"Populaires", "Récentes", "Favorites"};
        return game.ctx.fonts->bold.measure(28, tabs[i]) + 60;
    }
    float tabX(int i) const {
        float x = 80;
        for (int k = 0; k < i; ++k) x += tabW(k) + 14;
        return x;
    }
    void cardPos(int i, float &x, float &y) const {
        x = 60 + static_cast<float>(i % 2) * (kCardW + kGapX);
        y = kTop + static_cast<float>(i / 2) * (kCardH + kGapY) - smooth_;
    }
    float maxScroll() const {
        const int rows = (static_cast<int>(results_.size()) + 1) / 2;
        return std::max(0.0f, static_cast<float>(rows) * (kCardH + kGapY) - 600);
    }

    void renderCard(int i, float x, float y, const std::unordered_map<int, const net::DownloadState *> &byId, float in) {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const net::RemoteSet &s = results_[static_cast<size_t>(i)];
        const bool sel = i == cursor_;
        char line[256];
        x += (1 - in) * 300;
        ui::roundRect(c, x - 3, y - 3, kCardW + 6, kCardH + 6, 20, sel ? ui::color::blue : gfx::rgba(255, 255, 255, 25));
        ui::roundRect(c, x, y, kCardW, kCardH, 18, gfx::rgba(26, 24, 38));
        auto cover = covers_.find(s.id);
        if (cover != covers_.end()) {
            const float ta = static_cast<float>(cover->second.width) / static_cast<float>(cover->second.height);
            const float aspect = (kCardW - 8) / (kCardH - 8);
            float v0 = 0, v1 = 1, u0 = 0, u1 = 1;
            if (ta < aspect) {
                const float cut = (1 - ta / aspect) / 2;
                v0 = cut;
                v1 = 1 - cut;
            } else {
                const float cut = (1 - aspect / ta) / 2;
                u0 = cut;
                u1 = 1 - cut;
            }
            b.quad(cover->second, x + 4, y + 4, kCardW - 8, kCardH - 8, gfx::rgba(255, 255, 255, sel ? 255 : 190), u0, v0, u1, v1);
        }
        for (int k = 0; k < 12; ++k)
            b.rect(x + 4 + static_cast<float>(k) * 56, y + 4, 56, kCardH - 8, gfx::rgba(0, 0, 0, static_cast<uint8_t>(190 - k * 14)));
        const std::string title = c.fonts->bold.ellipsize(34, s.title, kCardW - 220);
        c.fonts->bold.drawShadow(b, x + 28, y + 16, 34, ui::color::text, title.c_str());
        snprintf(line, sizeof(line), "%s", s.artist.c_str());
        const std::string artist = c.fonts->semibold.ellipsize(26, line, kCardW - 220);
        c.fonts->semibold.drawShadow(b, x + 30, y + 58, 26, gfx::rgba(235, 230, 245), artist.c_str());
        snprintf(line, sizeof(line), "mappée par %s", s.creator.c_str());
        c.fonts->regular.drawShadow(b, x + 30, y + 92, 22, ui::color::textDim, line);
        // Statut et difficultés.
        const bool ranked = s.status == "ranked" || s.status == "approved";
        const uint32_t statusCol = ranked ? ui::color::lime : (s.status == "loved" ? ui::color::pink : ui::color::yellow);
        const char *status = ranked ? "CLASSÉE" : (s.status == "loved" ? "AIMÉE" : "AUTRE");
        ui::roundRect(c, x + 28, y + 126, 120, 30, 15, statusCol);
        c.fonts->bold.draw(b, x + 88, y + 129, 20, gfx::rgba(20, 20, 30), status, gfx::Align::Center);
        for (size_t d = 0; d < s.diffs.size() && d < 18; ++d) {
            const float dx = x + 174 + static_cast<float>(d) * 24;
            b.centered(c.tex->disc, dx, y + 141, 20, gfx::rgba(0, 0, 0, 160));
            b.centered(c.tex->ringThick, dx, y + 141, 18, ui::starColor(s.diffs[d].stars));
        }
        // Zone d'action à droite (comme le bouton de téléchargement des cartes lazer).
        const float ax = x + kCardW - 150;
        auto it = byId.find(s.id);
        const bool installed = c.library->hasSetId(s.id) || (it != byId.end() && it->second->phase.load() == 3);
        ui::roundRect(c, ax, y + 4, 146, kCardH - 8, 16,
                      installed ? gfx::rgba(60, 140, 70, 230) : (sel ? gfx::rgba(52, 140, 230, 230) : gfx::rgba(0, 0, 0, 150)));
        if (installed) {
            c.fonts->bold.draw(b, ax + 73, y + 64, 28, ui::color::text, "✓", gfx::Align::Center);
            c.fonts->semibold.draw(b, ax + 73, y + 100, 20, ui::color::text, "Installée", gfx::Align::Center);
        } else if (it != byId.end() && it->second->phase.load() == 4) {
            c.fonts->bold.draw(b, ax + 73, y + 70, 24, ui::color::red, "Échec", gfx::Align::Center);
        } else if (it != byId.end()) {
            const net::DownloadState *d = it->second;
            const int phase = d->phase.load();
            const double total = static_cast<double>(d->total.load()), got = static_cast<double>(d->received.load());
            const float frac = phase == 2 ? 1.0f : (total > 0 ? static_cast<float>(got / total) : 0.0f);
            // Anneau de progression.
            const int dots = 60;
            for (int k = 0; k < dots; ++k) {
                const float f = static_cast<float>(k) / dots, a = -1.5708f + f * 6.2831853f;
                b.centered(c.tex->disc, ax + 73 + std::cos(a) * 40, y + 70 + std::sin(a) * 40, 9,
                           f < frac ? ui::color::blue : gfx::rgba(255, 255, 255, 40));
            }
            snprintf(line, sizeof(line), "%.0f%%", frac * 100);
            c.fonts->bold.draw(b, ax + 73, y + 56, 24, ui::color::text, phase == 2 ? "…" : line, gfx::Align::Center);
            c.fonts->regular.draw(b, ax + 73, y + 124, 18, ui::color::textDim, phase == 0 ? "En attente" : (phase == 2 ? "Installation" : "Téléchargement"),
                                  gfx::Align::Center);
        } else {
            ui::icon(c, ui::Icon::Download, ax + 73, y + 66, 44, ui::color::text);
            snprintf(line, sizeof(line), "%s", ui::formatTime(s.lengthSec * 1000.0).c_str());
            c.fonts->semibold.draw(b, ax + 73, y + 112, 20, ui::color::textDim, line, gfx::Align::Center);
        }
    }

    void refresh() {
        const net::Sort sorts[] = {net::Sort::Popular, net::Sort::Recent, net::Sort::Favourites};
        game.mirror.search(query_, sorts[tab_], page_);
        error_.clear();
    }

    // Saisie au clavier (PC, ou clavier USB sur la console) : relance la recherche.
    void typing(const UiInput &in) {
        if (!in.raw || !in.raw->keyboard.connected) return;
        const bool *k = in.raw->keyboard.keys;
        bool changed = false;
        const bool shift = k[0xE1] || k[0xE5];
        for (int code = 0x04; code <= 0x2C; ++code) {
            if (!k[code] || prevKeys_[code]) continue;
            char ch = 0;
            if (code <= 0x1D) ch = static_cast<char>((shift ? 'A' : 'a') + (code - 0x04));
            else if (code <= 0x26) ch = static_cast<char>('1' + (code - 0x1E));
            else if (code == 0x27) ch = '0';
            else if (code == 0x2C && !query_.empty()) ch = ' ';
            else if (code == 0x2A && !query_.empty()) {
                query_.pop_back();
                changed = true;
            }
            if (ch && query_.size() < 60) {
                query_ += ch;
                changed = true;
            }
        }
        for (int i = 0; i < 256; ++i) prevKeys_[i] = k[i];
        if (changed) {
            lastType_ = game.ctx.now;
            searchPending_ = true;
        }
        if (searchPending_ && game.ctx.now - lastType_ > 0.6) {
            searchPending_ = false;
            page_ = 0;
            refresh();
        }
    }

    std::vector<net::RemoteSet> results_;
    std::string error_, query_;
    std::unordered_map<int, gfx::Texture> covers_;
    std::set<const net::DownloadState *> handled_;
    int tab_ = 0, page_ = 0, cursor_ = 0;
    float scroll_ = 0, smooth_ = 0;
    double enterTime_ = 0, lastType_ = 0;
    bool prevKeys_[256] = {};
    bool searchPending_ = false;
};

}  // namespace

std::unique_ptr<Screen> Game::makeDownload() { return std::make_unique<Download>(*this); }
