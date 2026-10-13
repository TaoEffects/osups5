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
void destroyTexture(Texture &texture);

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
    void rect(float x, float y, float w, float h, uint32_t color);
    void outline(float x, float y, float w, float h, float thickness, uint32_t color);
    void end();

    const Texture &white() const { return white_; }
    const BatchStats &lastFrame() const { return last_; }

private:
    struct Vertex {
        float x, y, u, v;
        uint32_t color;
    };
    struct Command {
        GLuint texture;
        uint32_t firstQuad;
        uint32_t quadCount;
    };

    void flush();

    static constexpr uint32_t kMaxQuads = 32768;

    GLuint program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLint scaleLoc_ = -1;
    Texture white_;
    std::vector<Vertex> vertices_;
    std::vector<Command> commands_;
    BatchStats current_, last_;
    float scaleX_ = 1, scaleY_ = 1;
};

}  // namespace gfx
