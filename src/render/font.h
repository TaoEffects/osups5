// SPDX-License-Identifier: GPL-3.0-or-later
// Police bitmap 8x8 (font8x8 de Daniel Hepper, domaine public), jeu Latin-1 complet
// donc les accents français s'affichent. Le texte d'entrée est en UTF-8.
#pragma once

#include "render/batch.h"

namespace gfx {

class BitmapFont {
public:
    bool init();
    void shutdown();

    // Dessine `utf8` en (x, y) avec des glyphes de 8*scale pixels. Renvoie la largeur.
    float draw(SpriteBatch &batch, float x, float y, float scale, uint32_t color, const char *utf8) const;
    // Variante avec ombre portée, plus lisible sur fond clair.
    float drawShadow(SpriteBatch &batch, float x, float y, float scale, uint32_t color, const char *utf8) const;
    static float width(float scale, const char *utf8);

private:
    Texture atlas_;
};

}  // namespace gfx
