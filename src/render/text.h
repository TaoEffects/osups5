// SPDX-License-Identifier: GPL-3.0-or-later
// Texte TrueType (polices libres OFL) rendu en champ de distance signé : chaque
// glyphe est précalculé une fois dans un atlas 1 canal, et le shader en tire des
// bords nets à n'importe quelle taille (plus de texte flou ou pixelisé quand on
// l'agrandit). Crénage de la police appliqué.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "render/batch.h"

namespace gfx {

enum class Align { Left, Center, Right };

class GlyphCache;

class Font {
public:
    // fallback : police consultée pour les caractères absents (peut être vide).
    // allGlyphs : charge tous les glyphes de la police (police d'icônes).
    bool load(const std::string &path, const std::string &fallback = "", bool allGlyphs = false);
    void shutdown();
    bool loaded() const { return atlas_.id != 0; }

    // (x, y) = coin haut gauche de la ligne. Renvoie la largeur dessinée.
    // spacing : espacement supplémentaire entre lettres (pixels, à cette taille).
    float draw(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *utf8,
               Align align = Align::Left, float spacing = 0) const;
    // Ombre douce sous le texte, comme lazer.
    float drawShadow(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *utf8,
                     Align align = Align::Left, float spacing = 0) const;
    // Variante bas niveau : épaississement / flou du champ de distance.
    float drawSdf(SpriteBatch &batch, float x, float y, float size, uint32_t color, const char *utf8, Align align,
                  float spacing, float dilate, float soft) const;
    float measure(float size, const char *utf8, float spacing = 0) const;
    // Coupe le texte avec "…" pour tenir dans maxWidth.
    std::string ellipsize(float size, const std::string &text, float maxWidth) const;
    // Hauteur des capitales (pour centrer verticalement), en fraction de la taille.
    float capHeight() const { return capHeight_; }
    float ascentPx() const { return ascent_; }
    // Ordonnée du haut de ligne qui centre les capitales sur `cy`.
    float topForCenter(float cy, float size) const { return cy + capHeight_ * size * 0.5f - ascent_ * size / bakePx_; }
    // Texte centré verticalement sur cy (hauteur des capitales).
    float drawMid(SpriteBatch &batch, float x, float cy, float size, uint32_t color, const char *utf8,
                  Align align = Align::Left, float spacing = 0) const {
        return draw(batch, x, topForCenter(cy, size), size, color, utf8, align, spacing);
    }
    // Police de secours à la demande (japonais...) : glyphes créés au besoin.
    void setDynamicFallback(GlyphCache *cache) { dyn_ = cache; }
    // Un seul glyphe centré exactement (boîte réelle du dessin) en (cx, cy) : icônes.
    void drawGlyphCentered(SpriteBatch &batch, const char *utf8, float cx, float cy, float size, uint32_t color, float angle = 0,
                           float sx = 1, float sy = 1) const;

private:
    struct Glyph {
        float x0, y0, x1, y1;  // boîte du bitmap relative au crayon (pixels à bakePx, y depuis le haut de ligne)
        float u0, v0, u1, v1;
        float advance;
        int index;             // index du glyphe dans sa police (crénage)
        bool fallback;
        const Texture *tex = nullptr;  // atlas (nullptr : celui de la police)
    };
    const Glyph *find(unsigned cp) const;
    float kern(const Glyph *a, const Glyph *b) const;

    Texture atlas_;
    float bakePx_ = 48;
    float ascent_ = 0;
    float capHeight_ = 0.7f;
    float scale_ = 1;  // unités de la police -> pixels à bakePx
    std::vector<uint8_t> ttf_;
    std::vector<uint8_t> info_;  // stbtt_fontinfo (opaque ici)
    std::unordered_map<unsigned, Glyph> glyphs_;
    mutable std::unordered_map<uint64_t, float> kernCache_;
    GlyphCache *dyn_ = nullptr;
    friend class GlyphCache;
};

// Glyphes d'une grosse police (CJK) rendus en champ de distance à la demande,
// dans un atlas 2048x2048 mis à jour par morceaux. Partagé entre les graisses.
class GlyphCache {
public:
    bool load(const std::string &path, float bakePx, float hostAscent);
    void shutdown();
    bool has(unsigned cp) const;
    const Font::Glyph *get(unsigned cp);

private:
    std::vector<uint8_t> ttf_, info_;
    Texture atlas_;
    float scale_ = 1, ascent_ = 0;
    int penX_ = 1, penY_ = 1, shelf_ = 0;
    std::unordered_map<unsigned, Font::Glyph> glyphs_;
    static constexpr int kSize = 2048;
};

// Décode un point de code UTF-8 et avance le pointeur.
unsigned decodeUtf8(const char *&s);

}  // namespace gfx
