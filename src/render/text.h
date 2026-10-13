// SPDX-License-Identifier: GPL-3.0-or-later
// Texte TrueType (police Inter, licence OFL) : glyphes précalculés une fois dans un
// atlas 1 canal, puis dessinés comme des sprites du lot courant.
#pragma once

#include <string>
#include <unordered_map>

#include "render/batch.h"

namespace gfx {

enum class Align { Left, Center, Right };

class Font {
public:
    // bakePx : taille de référence de l'atlas ; le texte peut être dessiné plus petit.
    bool load(const std::string &path, float bakePx, int atlasW, int atlasH);
    void shutdown();
    bool loaded() const { return atlas_.id != 0; }

    // (x, y) = coin haut gauche de la ligne. Renvoie la largeur dessinée.
    float draw(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *utf8,
               Align align = Align::Left) const;
    float drawShadow(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *utf8,
                     Align align = Align::Left) const;
    float measure(float size, const char *utf8) const;
    // Coupe le texte avec "…" pour tenir dans maxWidth.
    std::string ellipsize(float size, const std::string &text, float maxWidth) const;

private:
    struct Glyph {
        float x0, y0, x1, y1;      // position relative (pixels de l'atlas, à la taille bakePx)
        float u0, v0, u1, v1;
        float advance;
    };
    const Glyph *find(unsigned cp) const;

    Texture atlas_;
    float bakePx_ = 0;
    float ascent_ = 0;
    std::unordered_map<unsigned, Glyph> glyphs_;
};

// Décode un point de code UTF-8 et avance le pointeur.
unsigned decodeUtf8(const char *&s);

}  // namespace gfx
