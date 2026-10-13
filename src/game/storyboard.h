// SPDX-License-Identifier: GPL-3.0-or-later
// Storyboards osu! : sprites et animations décrits dans la section [Events] du
// .osu et dans le .osb du dossier. Commandes F, M, MX, MY, S, V, R, C, P et
// boucles L ; variables [Variables] du .osb. Les déclencheurs (T) et les sons
// de storyboard ne sont pas gérés. Images chargées en arrière-plan.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/batch.h"
#include "render/image.h"

namespace game {

class Storyboard {
public:
    ~Storyboard();
    // folder : dossier de la map ; osuFile : nom du .osu joué. Renvoie false s'il n'y a rien à afficher.
    bool load(const std::string &folder, const std::string &osuFile);
    void unload();
    bool empty() const { return elements_.empty(); }
    // Vrai si le storyboard réutilise l'image de fond : on cache alors celle-ci, comme osu!.
    bool usesFile(const std::string &file) const;
    // À appeler à chaque image : envoie au GPU les images décodées.
    void update();
    // Couches sous les objets (fond, réussite, premier plan) ou au-dessus (overlay).
    void drawBelow(gfx::SpriteBatch &b, double timeMs) const { draw(b, timeMs, false); }
    void drawAbove(gfx::SpriteBatch &b, double timeMs) const { draw(b, timeMs, true); }

private:
    enum Prop { Fade, Move, MoveX, MoveY, Scale, VScale, Rotate, Colour, Param, PropCount };
    struct Cmd {
        double t0, t1;
        float a[3], b[3];
        uint8_t easing;
    };
    struct Element {
        int layer = 0, origin = 1;
        float x = 320, y = 240;
        std::vector<int> frames;  // index de texture par image d'animation
        double frameDelay = 0;
        bool loopForever = true;
        std::vector<Cmd> cmds[PropCount];
        double start = 1e18, end = -1e18;
    };

    void parseEvents(const std::string &text, const std::string &folder);
    int texture(const std::string &relPath, const std::string &folder);
    void draw(gfx::SpriteBatch &b, double t, bool overlay) const;
    static bool value(const std::vector<Cmd> &cmds, double t, int n, float *out);

    std::vector<Element> elements_;
    std::vector<gfx::Texture> textures_;
    std::unordered_map<std::string, int> texByPath_;
    std::unordered_map<std::string, std::string> files_;  // chemin en minuscules -> chemin réel
    std::unordered_map<uint64_t, int> pending_;           // ticket -> texture
    std::vector<std::string> usedFiles_;
    gfx::ImageLoader loader_;
    bool loaderStarted_ = false;
};

}  // namespace game
