// SPDX-License-Identifier: GPL-3.0-or-later
// Rendu 2D par lots. Tous les quads d'une image sont accumulés côté CPU puis envoyés
// au GPU en UN seul transfert ; un appel de dessin n'est émis que lorsque la texture
// change. C'est le point 3 du brief : sans regroupement, le portage de Half-Life
// tournait à ~1 FPS sur PS5.
#pragma once

#include <cstdint>
#include <vector>

#include "render/gl.h"

namespace gfx {

// Couleur empaquetée dans l'ordre mémoire R, G, B, A.
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}
constexpr uint32_t withAlpha(uint32_t color, float a) {
    const float c = a < 0 ? 0 : (a > 1 ? 1 : a);
    return (color & 0x00FFFFFFu) | (static_cast<uint32_t>(static_cast<float>(color >> 24) * c) << 24);
}

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

Texture createTexture(int width, int height, const uint8_t *rgbaPixels, bool linearFilter);
// Texture 1 canal lue comme (1, 1, 1, alpha) : police, masques. 4x moins de mémoire.
Texture createAlphaTexture(int width, int height, const uint8_t *alphaPixels, bool linearFilter);
// Remplace le contenu (même taille, RGBA).
void updateTexture(const Texture &texture, const uint8_t *rgbaPixels);

// Premultiplied : pour composer une cible de rendu (couleur déjà multipliée par alpha).
enum class Blend : uint8_t { Alpha, Additive, Premultiplied };

// Cible multiéchantillonnée (MSAA) : on dessine toute l'image dedans, puis on la
// "résout" vers l'écran. Supprime l'aliasing des bords (quads inclinés, traits...).
struct MsaaTarget {
    GLuint fbo = 0, color = 0, stencil = 0;
    int width = 0, height = 0, samples = 0;
    bool create(int width, int height, int samples);
    void destroy();
    void resolveToScreen() const;
};

// Cible de rendu hors écran (texture couleur RGBA).
struct RenderTarget {
    GLuint fbo = 0;
    Texture color;
    bool create(int width, int height);
    void destroy();
};
void destroyTexture(Texture &texture);

// Couleurs des 4 coins d'une forme (haut gauche, haut droit, bas droit, bas gauche).
struct Corners {
    uint32_t tl, tr, br, bl;
    static Corners vertical(uint32_t top, uint32_t bottom) { return {top, top, bottom, bottom}; }
    static Corners horizontal(uint32_t left, uint32_t right) { return {left, right, right, left}; }
    static Corners solid(uint32_t c) { return {c, c, c, c}; }
};

struct BatchStats {
    int drawCalls = 0;
    int quads = 0;
    int flushes = 0;        // envois de sommets (idéalement 1 par image)
    size_t bytesUploaded = 0;
};

class SpriteBatch {
public:
    bool init();
    void shutdown();

    // Commence une image : coordonnées virtuelles (0,0) en haut à gauche jusqu'à
    // (virtualW, virtualH), étirées sur un viewport de viewportW x viewportH pixels.
    void begin(float virtualW, float virtualH, int viewportW, int viewportH);
    void quad(const Texture &tex, float x, float y, float w, float h, uint32_t color, float u0 = 0, float v0 = 0,
              float u1 = 1, float v1 = 1);
    void centered(const Texture &tex, float cx, float cy, float size, uint32_t color);
    // Quad tourné autour de son centre (angle en radians).
    void rotated(const Texture &tex, float cx, float cy, float w, float h, float angle, uint32_t color);
    // Quad quelconque (4 coins dans l'ordre), UV du rectangle entier.
    void quad4(const Texture &tex, const float xy[8], uint32_t color);
    // Segment épais.
    void line(float x0, float y0, float x1, float y1, float thickness, uint32_t color);
    void rect(float x, float y, float w, float h, uint32_t color);
    // Rectangle à dégradé vertical.
    void gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
    void setBlend(Blend blend) { blend_ = blend; }
    Blend blend() const { return blend_; }
    // Change de cible (nullptr = écran). Vide le lot en cours.
    void setTarget(const RenderTarget *target);
    // Framebuffer qui fait office d'"écran" (MSAA), 0 = écran réel.
    void setScreenFramebuffer(GLuint fbo) { screenFbo_ = fbo; }
    void outline(float x, float y, float w, float h, float thickness, uint32_t color);

    // Formes vectorielles (champ de distance dans le shader) : bords nets à toute
    // taille. shear > 0 incline le haut vers la droite, comme le "shear" de lazer.
    void roundRect(float x, float y, float w, float h, float radius, uint32_t color, float shear = 0);
    void roundRectGradient(float x, float y, float w, float h, float radius, const Corners &colors, float shear = 0);
    void roundRectBorder(float x, float y, float w, float h, float radius, float border, uint32_t color, float shear = 0);
    // Ombre douce (flou ~ blur pixels) de la forme d'un rectangle arrondi.
    void shadow(float x, float y, float w, float h, float radius, float blur, uint32_t color, float shear = 0);
    void circle(float cx, float cy, float radius, uint32_t color, float soft = 0);
    void ring(float cx, float cy, float radius, float thickness, uint32_t color);
    // Triangle équilatéral pointe en haut, centré sur son centre de gravité.
    void triangle(float cx, float cy, float side, uint32_t color, float border = 0, float angle = 0);
    void triangleGradient(float cx, float cy, float side, uint32_t top, uint32_t bottom, float border = 0, float angle = 0);
    // Glyphe d'une police en champ de distance (dilate > 0 = plus gras, soft = flou).
    void sdfQuad(const Texture &tex, float x, float y, float w, float h, uint32_t color, float u0, float v0, float u1,
                 float v1, float dilate = 0, float soft = 0);
    // Arc d'anneau (rayon extérieur, épaisseur), de `from` à `to` en fractions de
    // tour, sens horaire depuis midi. Couleurs aux coins du carré englobant.
    void arc(float cx, float cy, float radius, float thickness, float from, float to, const Corners &colors);
    // Image découpée en rectangle arrondi (incliné ou non), bords anticrénelés.
    void roundRectTex(const Texture &tex, float x, float y, float w, float h, float radius, uint32_t color, float u0 = 0,
                      float v0 = 0, float u1 = 1, float v1 = 1, float shear = 0);
    // Masque : beginMask(), dessiner la forme, useMask(), dessiner le contenu,
    // endMask(). Un seul niveau (pas d'imbrication).
    void beginMask();
    void useMask();
    void endMask();
    void shape(float mode, float cx, float cy, float hw, float hh, float radius, float border, float soft,
               const Corners &colors, float shear = 0, float angle = 0);

    // Transformations empilées (translation, échelle, opacité) appliquées à tout ce
    // qui est dessiné ensuite : base des animations d'écran.
    void pushTransform(float tx, float ty, float scale = 1, float alpha = 1);
    void pushScaleAround(float cx, float cy, float scale, float alpha = 1);
    void pushAlpha(float alpha) { pushTransform(0, 0, 1, alpha); }
    void popTransform();
    float currentAlpha() const { return xf_.back().alpha; }
    // Découpage rectangulaire (coordonnées virtuelles, transformées).
    void pushClip(float x, float y, float w, float h);
    void popClip();

    void end();

    const Texture &white() const { return white_; }
    const BatchStats &lastFrame() const { return last_; }

private:
    struct Vertex {
        float x, y, u, v;
        uint32_t color;
        float lx = 0, ly = 0, hw = 0, hh = 0;          // forme : position locale, demi-taille
        float radius = 0, border = 0, soft = 0, mode = 0;
    };
    struct Command {
        GLuint texture;
        Blend blend;
        uint32_t firstQuad;
        uint32_t quadCount;
        int clip;
    };
    struct Xf {
        float scale = 1, tx = 0, ty = 0, alpha = 1;
    };
    struct Clip {
        float x, y, w, h;
    };
    std::vector<Xf> xf_ = std::vector<Xf>(1);
    std::vector<Clip> clips_, clipTable_;
    std::vector<uint32_t> clipIds_;
    const RenderTarget *target_ = nullptr;
    GLint premulLoc_ = -1, maskLoc_ = -1;
    int maskDepth_ = 0;
    bool maskWriting_ = false;

    void flush();

    static constexpr uint32_t kMaxQuads = 32768;

    GLuint program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLint scaleLoc_ = -1, offsetLoc_ = -1;
    float offsetY_ = 1.0f;
    Texture white_;
    std::vector<Vertex> vertices_;
    std::vector<Command> commands_;
    BatchStats current_, last_;
    float scaleX_ = 1, scaleY_ = 1;
    float virtualW_ = 1, virtualH_ = 1;
    int viewportW_ = 1, viewportH_ = 1;
    Blend blend_ = Blend::Alpha;
    GLuint screenFbo_ = 0;
    void push(const Texture &tex, const Vertex v[4]);
};

}  // namespace gfx
