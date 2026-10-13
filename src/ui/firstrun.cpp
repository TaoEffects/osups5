// SPDX-License-Identifier: GPL-3.0-or-later
// Premier lancement, sur le modèle du FirstRunSetupOverlay de lazer : on récupère
// le tutoriel et une sélection de maps de base (les mêmes que celles que lazer
// propose, BundledBeatmapDownloader), via le miroir — jamais via les serveurs
// officiels. Les téléchargements continuent en arrière-plan si on passe.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "app/game.h"
#include "game/library.h"
#include "ui/lz.h"

namespace {

using namespace lz;

// Identifiants de sets (BundledBeatmapDownloader de lazer) : tutoriel puis
// la liste « toujours incluse ».
constexpr int kTutorial = 1011011;
const int kBundled[] = {123593,  140662,  151878,  163112,  190390,  241526,  299224,  319473,  342751,  385056,
                        456054,  1388906, 1841885, 1971987, 2412244, 2412260, 2412232, 2412292, 2412328, 2412331};

class FirstRun : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        Context &c = game.ctx;
        const double t = ms(c);
        game.toolbar.show(c, false);
        game.logo.opacity.go(t, 0, 300, Ease::OutQuint);
        game.setBackground(0.4f, 0, 500);
        panelY_.fromTo(t, 200 * U, 0, 600, Ease::OutQuint);
        panelA_.fromTo(t, 0, 1, 400, Ease::OutQuint);
        // Lancement immédiat : le tutoriel d'abord.
        auto start = [&](int id) {
            if (c.library->hasSetId(id)) return;
            net::RemoteSet s;
            s.id = id;
            states_.push_back(game.mirror.download(s, true));
        };
        start(kTutorial);
        for (int id : kBundled) start(id);
        c.settings->firstRunDone = true;
        c.saveSettings();
        c.play(c.sfx->uiOpen, 0.7f);
    }

    double leave() override {
        const double t = ms(game.ctx);
        panelA_.go(t, 0, 300, Ease::OutQuint);
        panelY_.go(t, 100 * U, 300, Ease::OutQuint);
        return 300;
    }

    void update(const UiInput &in) override {
        Context &c = game.ctx;
        nextFx_.setHover(c, in.hover(nextX(), nextY(), 200 * U, 50 * U));
        if (in.confirm || in.back || (nextFx_.hovered && in.mouseClick)) {
            nextFx_.trigger(c);
            c.play(c.sfx->uiSelect);
            if (done() < static_cast<int>(states_.size()))
                game.toast("Les maps continuent de se télécharger en arrière-plan");
            game.goTo(game.makeMainMenu());
        }
    }

    void render() override {
        Context &c = game.ctx;
        gfx::SpriteBatch &b = *c.batch;
        const double t = ms(c);
        static const Palette pal = palette(hue::purple);
        b.pushTransform(0, panelY_.at(t), 1, panelA_.at(t));
        const float w = 900 * U, h = 520 * U;
        const float x = kVW / 2 - w / 2, y = kVH / 2 - h / 2;
        b.shadow(x, y + 4 * U, w, h, 10 * U, 20 * U, alpha(col::black, 0.5f));
        b.roundRect(x, y, w, h, 10 * U, pal.background4);
        // En-tête (ShearedOverlayHeader).
        b.roundRect(x, y, w, 90 * U, 10 * U, pal.background5);
        c.fonts->bold.draw(b, x + 30 * U, y + 18 * U, 30 * U, col::white, "configuration initiale");
        c.fonts->regular.draw(b, x + 30 * U, y + 56 * U, 16 * U, pal.content2, "bienvenue ! quelques maps pour commencer");
        float ty = y + 112 * U;
        const char *lines[] = {"Pour ne pas démarrer avec une liste vide, le jeu télécharge le tutoriel",
                               "et une vingtaine de maps de base, comme le jeu d'origine.",
                               "Elles viennent du miroir osu.direct ; aucun compte n'est nécessaire."};
        for (const char *l : lines) {
            c.fonts->regular.draw(b, x + 30 * U, ty, 17 * U, col::white, l);
            ty += 26 * U;
        }
        ty += 14 * U;
        // Deux lignes de progression : tutoriel, maps de base.
        auto row = [&](const char *label, int first, int count) {
            int fin = 0, failed = 0;
            float frac = 0;
            for (int i = first; i < first + count && i < static_cast<int>(states_.size()); ++i) {
                const net::DownloadState &d = *states_[static_cast<size_t>(i)];
                const int ph = d.phase.load();
                if (ph == 3) ++fin;
                if (ph == 4) ++failed;
                const double total = static_cast<double>(d.total.load());
                frac += ph >= 3 ? 1.0f : total > 0 ? static_cast<float>(static_cast<double>(d.received.load()) / total) * 0.95f : 0;
            }
            const int n = std::min(count, static_cast<int>(states_.size()) - first);
            if (n <= 0) {
                c.fonts->semibold.draw(b, x + 30 * U, ty, 18 * U, col::white, label);
                icon(c, fa::check, x + w - 40 * U, ty + 11 * U, 16 * U, col::lime1);
                c.fonts->regular.draw(b, x + w - 60 * U, ty + 1 * U, 16 * U, pal.content2, "déjà installé", gfx::Align::Right);
                ty += 60 * U;
                return;
            }
            frac /= static_cast<float>(n);
            c.fonts->semibold.draw(b, x + 30 * U, ty, 18 * U, col::white, label);
            char buf[64];
            snprintf(buf, sizeof(buf), failed ? "%d / %d (%d échecs)" : "%d / %d", fin, n, failed);
            c.fonts->regular.draw(b, x + w - 30 * U, ty + 1 * U, 16 * U, pal.content2, buf, gfx::Align::Right);
            const float bw = w - 60 * U;
            b.roundRect(x + 30 * U, ty + 30 * U, bw, 8 * U, 4 * U, pal.background6);
            b.roundRect(x + 30 * U, ty + 30 * U, std::max(8 * U, bw * frac), 8 * U, 4 * U, fin + failed >= n ? col::lime1 : col::pink1);
            ty += 60 * U;
        };
        const bool tutoQueued = !states_.empty() && states_[0]->setId == kTutorial;
        row("Tutoriel", 0, tutoQueued ? 1 : 0);
        row("Maps de base", tutoQueued ? 1 : 0, static_cast<int>(states_.size()) - (tutoQueued ? 1 : 0));
        // Bouton « continuer ».
        const float hv = nextFx_.hover.at(t);
        b.roundRect(nextX(), nextY(), 200 * U, 50 * U, 7 * U, lighten(col::purple1, 0.2f * hv), kShear);
        const float fl = nextFx_.flash.at(t);
        if (fl > 0) b.roundRect(nextX(), nextY(), 200 * U, 50 * U, 7 * U, alpha(col::white, fl), kShear);
        c.fonts->bold.draw(b, nextX() + 100 * U, nextY() + 14 * U, 20 * U, col::white, "Continuer", gfx::Align::Center);
        padGlyph(c, Pad::Cross, nextX() - 26 * U, nextY() + 25 * U, 26 * U);
        b.popTransform();
    }

private:
    float nextX() const { return kVW / 2 + 450 * U - 230 * U; }
    float nextY() const { return kVH / 2 + 260 * U - 74 * U; }
    int done() const {
        int n = 0;
        for (const auto &d : states_) n += d->phase.load() >= 3;
        return n;
    }

    std::vector<std::shared_ptr<net::DownloadState>> states_;
    Tween panelY_{0}, panelA_{0};
    ButtonFx nextFx_;
};

}  // namespace

std::unique_ptr<Screen> Game::makeFirstRun() { return std::make_unique<FirstRun>(*this); }
