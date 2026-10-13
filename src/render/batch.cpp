// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/batch.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace gfx {
namespace {

const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aShape;   // position locale (x, y), demi-taille (w, h)
layout(location = 4) in vec4 aParams;  // rayon, bordure, flou, mode
uniform vec2 uScale;
uniform vec2 uOffset;
out vec2 vUV;
out vec4 vColor;
out vec4 vShape;
flat out vec4 vParams;
void main() {
    vUV = aUV;
    vColor = aColor;
    vShape = aShape;
    vParams = aParams;
    gl_Position = vec4(aPos * uScale + uOffset, 0.0, 1.0);
}
)";

// Modes : 0 sprite, 1 rectangle arrondi (SDF), 2 triangle équilatéral (SDF),
// 3 glyphe en champ de distance. Les formes sont anticrénelées au pixel près
// quelle que soit leur taille ou leur inclinaison (fwidth).
const char *kFragmentShader = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in vec4 vShape;
flat in vec4 vParams;
uniform sampler2D uTex;
uniform float uPremul;
uniform float uMask;
out vec4 oColor;
float sdBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float sdTri(vec2 p, float r) {
    const float k = 1.7320508;
    p.y = -p.y;
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}
void main() {
    float mode = vParams.w;
    if (mode < 0.5) {
        oColor = texture(uTex, vUV) * vColor;
        if (uMask > 0.5 && oColor.a < 0.5) discard;
        return;
    }
    float a;
    if (mode > 2.5 && mode < 3.5) {
        float dist = texture(uTex, vUV).a;
        float w = max(fwidth(dist) * 0.75, 1e-4) + vParams.z;
        a = smoothstep(0.5 - w, 0.5 + w, dist + vParams.y);
    } else if (mode > 3.5) {
        // Arc d'anneau : de vUV.x à vUV.y (fractions de tour, sens horaire depuis midi).
        float r = length(vShape.xy);
        float d = abs(r - (vParams.x - vParams.y * 0.5)) - vParams.y * 0.5;
        float ang = atan(vShape.x, -vShape.y) / 6.2831853;
        if (ang < 0.0) ang += 1.0;
        float outside = max(vUV.x - ang, ang - vUV.y);
        d = max(d, outside * 6.2831853 * max(r, 1.0));
        float aa = max(fwidth(d) * 0.75, 1e-4);
        a = 1.0 - smoothstep(-aa, aa, d);
    } else {
        float d = mode < 1.5 ? sdBox(vShape.xy, vShape.zw, vParams.x) : sdTri(vShape.xy, vShape.z);
        float aa = max(fwidth(d) * 0.75, 1e-4) + vParams.z;
        a = 1.0 - smoothstep(-aa, aa, d);
        if (vParams.y > 0.0) a *= smoothstep(-aa, aa, d + vParams.y);
    }
    vec4 tex = mode > 2.5 ? vec4(1.0) : texture(uTex, vUV);
    float alpha = vColor.a * tex.a * a;
    if (uMask > 0.5 && alpha < 0.5) discard;
    vec3 rgb = vColor.rgb * tex.rgb;
    oColor = vec4(mix(rgb, rgb * alpha, uPremul), alpha);
}
)";

GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024] = {};
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        LOG("gl: echec compilation shader: %s", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

Texture createTexture(int width, int height, const uint8_t *rgbaPixels, bool linearFilter) {
    Texture t;
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

Texture createAlphaTexture(int width, int height, const uint8_t *alphaPixels, bool linearFilter) {
    Texture t;
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, alphaPixels);
    const GLint swizzle[4] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void updateTexture(const Texture &texture, const uint8_t *rgbaPixels) {
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
}

bool RenderTarget::create(int width, int height) {
    color = createTexture(width, height, nullptr, true);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.id, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) LOG("gl: framebuffer %dx%d incomplet", width, height);
    return ok;
}

bool MsaaTarget::create(int w, int h, int s) {
    width = w;
    height = h;
    samples = s;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_RGBA8, w, h);
    // Stencil : masques (contenu découpé dans une forme arrondie ou inclinée).
    glGenRenderbuffers(1, &stencil);
    glBindRenderbuffer(GL_RENDERBUFFER, stencil);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_DEPTH24_STENCIL8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        // Repli : sans stencil (les masques ne découperont plus, le reste marche).
        glDeleteRenderbuffers(1, &stencil);
        stencil = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        const bool ok2 = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (ok2) {
            LOG("gl: MSAA x%d %dx%d actif (sans stencil)", s, w, h);
            return true;
        }
        LOG("gl: MSAA x%d %dx%d indisponible", s, w, h);
        destroy();
    } else {
        LOG("gl: MSAA x%d %dx%d actif", s, w, h);
    }
    return ok;
}

void MsaaTarget::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (color) glDeleteRenderbuffers(1, &color);
    if (stencil) glDeleteRenderbuffers(1, &stencil);
    fbo = color = stencil = 0;
}

void MsaaTarget::resolveToScreen() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    fbo = 0;
    destroyTexture(color);
}

void destroyTexture(Texture &texture) {
    if (texture.id) glDeleteTextures(1, &texture.id);
    texture = Texture{};
}

bool SpriteBatch::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[1024] = {};
        glGetProgramInfoLog(program_, sizeof(info), nullptr, info);
        LOG("gl: echec edition de liens: %s", info);
        return false;
    }
    scaleLoc_ = glGetUniformLocation(program_, "uScale");
    offsetLoc_ = glGetUniformLocation(program_, "uOffset");
    premulLoc_ = glGetUniformLocation(program_, "uPremul");
    maskLoc_ = glGetUniformLocation(program_, "uMask");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uTex"), 0);

    // Index statiques : chaque quad = 2 triangles (0,1,2) (2,3,0).
    std::vector<uint32_t> indices(kMaxQuads * 6);
    for (uint32_t q = 0; q < kMaxQuads; ++q) {
        const uint32_t v = q * 4;
        uint32_t *i = &indices[q * 6];
        i[0] = v;
        i[1] = v + 1;
        i[2] = v + 2;
        i[3] = v + 2;
        i[4] = v + 3;
        i[5] = v;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(8));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void *>(16));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(20));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(36));
    glBindVertexArray(0);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    white_ = createTexture(1, 1, whitePixel, false);
    vertices_.reserve(4096 * 4);
    commands_.reserve(64);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) LOG("gl: erreur 0x%x pendant l'init du batch", err);
    return err == GL_NO_ERROR;
}

void SpriteBatch::shutdown() {
    destroyTexture(white_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    vbo_ = ibo_ = vao_ = program_ = 0;
}

void SpriteBatch::begin(float virtualW, float virtualH, int viewportW, int viewportH) {
    current_ = BatchStats{};
    vertices_.clear();
    commands_.clear();
    scaleX_ = 2.0f / virtualW;
    scaleY_ = -2.0f / virtualH;
    virtualW_ = virtualW;
    virtualH_ = virtualH;
    viewportW_ = viewportW;
    viewportH_ = viewportH;
    blend_ = Blend::Alpha;
    offsetY_ = 1.0f;
    xf_.assign(1, Xf{});
    clips_.clear();
    target_ = nullptr;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, screenFbo_);
    glViewport(0, 0, viewportW, viewportH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void SpriteBatch::push(const Texture &tex, const Vertex in[4]) {
    if (vertices_.size() / 4 >= kMaxQuads) flush();
    const Xf &t = xf_.back();
    Vertex v[4] = {in[0], in[1], in[2], in[3]};
    if (t.scale != 1 || t.tx != 0 || t.ty != 0 || t.alpha < 1) {
        for (Vertex &q : v) {
            q.x = q.x * t.scale + t.tx;
            q.y = q.y * t.scale + t.ty;
            if (t.alpha < 1) q.color = withAlpha(q.color, t.alpha);
        }
    }
    const uint32_t quadIndex = static_cast<uint32_t>(vertices_.size() / 4);
    const int clip = clips_.empty() ? -1 : static_cast<int>(clipIds_.back());
    if (commands_.empty() || commands_.back().texture != tex.id || commands_.back().blend != blend_ ||
        commands_.back().clip != clip) {
        commands_.push_back(Command{tex.id, blend_, quadIndex, 0, clip});
    }
    commands_.back().quadCount++;
    vertices_.insert(vertices_.end(), v, v + 4);
}

void SpriteBatch::pushTransform(float tx, float ty, float scale, float alpha) {
    const Xf &p = xf_.back();
    Xf n;
    n.scale = p.scale * scale;
    n.tx = p.tx + tx * p.scale;
    n.ty = p.ty + ty * p.scale;
    n.alpha = p.alpha * alpha;
    xf_.push_back(n);
}

void SpriteBatch::pushScaleAround(float cx, float cy, float scale, float alpha) {
    pushTransform(cx - cx * scale, cy - cy * scale, scale, alpha);
}

void SpriteBatch::popTransform() {
    if (xf_.size() > 1) xf_.pop_back();
}

void SpriteBatch::pushClip(float x, float y, float w, float h) {
    const Xf &t = xf_.back();
    Clip c{x * t.scale + t.tx, y * t.scale + t.ty, w * t.scale, h * t.scale};
    if (!clips_.empty()) {
        const Clip &p = clips_.back();
        const float x1 = std::min(c.x + c.w, p.x + p.w), y1 = std::min(c.y + c.h, p.y + p.h);
        c.x = std::max(c.x, p.x);
        c.y = std::max(c.y, p.y);
        c.w = std::max(0.0f, x1 - c.x);
        c.h = std::max(0.0f, y1 - c.y);
    }
    clips_.push_back(c);
    clipIds_.push_back(static_cast<uint32_t>(clipTable_.size()));
    clipTable_.push_back(c);
}

void SpriteBatch::popClip() {
    if (clips_.empty()) return;
    clips_.pop_back();
    clipIds_.pop_back();
}

void SpriteBatch::beginMask() {
    flush();
    if (maskDepth_ > 0) LOG("batch: masques imbriqués non gérés");
    ++maskDepth_;
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    maskWriting_ = true;
}

void SpriteBatch::useMask() {
    flush();
    maskWriting_ = false;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void SpriteBatch::endMask() {
    flush();
    maskDepth_ = std::max(0, maskDepth_ - 1);
    glDisable(GL_STENCIL_TEST);
}

void SpriteBatch::arc(float cx, float cy, float radius, float thickness, float from, float to, const Corners &col) {
    if (to <= from) return;
    const float e = radius + 2;
    const float lx[4] = {-e, e, e, -e}, ly[4] = {-e, -e, e, e};
    const uint32_t cc[4] = {col.tl, col.tr, col.br, col.bl};
    Vertex v[4];
    for (int i = 0; i < 4; ++i) v[i] = Vertex{cx + lx[i], cy + ly[i], from, to, cc[i], lx[i], ly[i], e, e, radius, thickness, 0, 4};
    push(white_, v);
}

void SpriteBatch::roundRectTex(const Texture &tex, float x, float y, float w, float h, float radius, uint32_t color,
                               float u0, float v0, float u1, float v1, float shear) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    const float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    // Pas de marge ici : les UV couvrent exactement la forme.
    const float lx[4] = {-hw, hw, hw, -hw}, ly[4] = {-hh, -hh, hh, hh};
    const float uu[4] = {u0, u1, u1, u0}, vv[4] = {v0, v0, v1, v1};
    Vertex v[4];
    for (int i = 0; i < 4; ++i)
        v[i] = Vertex{cx + lx[i] - shear * ly[i], cy + ly[i], uu[i], vv[i], color, lx[i], ly[i], hw, hh, radius, 0, 0, 1};
    push(tex, v);
}

void SpriteBatch::shape(float mode, float cx, float cy, float hw, float hh, float radius, float border, float soft,
                        const Corners &col, float shear, float angle) {
    // Le quad déborde de la forme pour laisser la place à l'anticrénelage et au flou.
    const float pad = soft * 2.0f + 2.0f;
    const float ex = hw + pad, ey = hh + pad;
    const float lx[4] = {-ex, ex, ex, -ex}, ly[4] = {-ey, -ey, ey, ey};
    const uint32_t cc[4] = {col.tl, col.tr, col.br, col.bl};
    const float c = std::cos(angle), s = std::sin(angle);
    Vertex v[4];
    for (int i = 0; i < 4; ++i) {
        // Inclinaison façon lazer : le haut part vers la droite ("/").
        const float px = lx[i] - shear * ly[i], py = ly[i];
        const float rx = angle != 0 ? px * c - py * s : px, ry = angle != 0 ? px * s + py * c : py;
        v[i] = Vertex{cx + rx, cy + ry, 0, 0, cc[i], lx[i], ly[i], hw, hh, radius, border, soft, mode};
    }
    push(white_, v);
}

void SpriteBatch::roundRect(float x, float y, float w, float h, float radius, uint32_t color, float shear) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    shape(1, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, radius, 0, 0, Corners{color, color, color, color}, shear);
}

void SpriteBatch::roundRectGradient(float x, float y, float w, float h, float radius, const Corners &colors, float shear) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    shape(1, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, radius, 0, 0, colors, shear);
}

void SpriteBatch::roundRectBorder(float x, float y, float w, float h, float radius, float border, uint32_t color,
                                  float shear) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    shape(1, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, radius, border, 0, Corners{color, color, color, color},
          shear);
}

void SpriteBatch::shadow(float x, float y, float w, float h, float radius, float blur, uint32_t color, float shear) {
    radius = std::min(radius, std::min(w, h) * 0.5f);
    shape(1, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, radius, 0, blur, Corners{color, color, color, color}, shear);
}

void SpriteBatch::circle(float cx, float cy, float r, uint32_t color, float soft) {
    shape(1, cx, cy, r, r, r, 0, soft, Corners{color, color, color, color});
}

void SpriteBatch::ring(float cx, float cy, float r, float thickness, uint32_t color) {
    shape(1, cx, cy, r, r, r, thickness, 0, Corners{color, color, color, color});
}

void SpriteBatch::triangle(float cx, float cy, float side, uint32_t color, float border, float angle) {
    triangleGradient(cx, cy, side, color, color, border, angle);
}

void SpriteBatch::triangleGradient(float cx, float cy, float side, uint32_t top, uint32_t bottom, float border,
                                   float angle) {
    const float r = side * 0.5f;
    // sdTri est centré sur le centre de gravité ; le quad entoure le triangle.
    // La pointe est à 1,155 r du centre de gravité : le quad doit monter jusque-là.
    shape(2, cx, cy, r, r * 1.2f, 0, border, 0, Corners{top, top, bottom, bottom}, 0, angle);
}

void SpriteBatch::sdfQuad(const Texture &tex, float x, float y, float w, float h, uint32_t color, float u0, float v0,
                          float u1, float v1, float dilate, float soft) {
    const Vertex v[4] = {{x, y, u0, v0, color, 0, 0, 0, 0, 0, dilate, soft, 3},
                         {x + w, y, u1, v0, color, 0, 0, 0, 0, 0, dilate, soft, 3},
                         {x + w, y + h, u1, v1, color, 0, 0, 0, 0, 0, dilate, soft, 3},
                         {x, y + h, u0, v1, color, 0, 0, 0, 0, 0, dilate, soft, 3}};
    push(tex, v);
}

void SpriteBatch::quad(const Texture &tex, float x, float y, float w, float h, uint32_t color, float u0, float v0,
                       float u1, float v1) {
    const Vertex v[4] = {{x, y, u0, v0, color}, {x + w, y, u1, v0, color}, {x + w, y + h, u1, v1, color},
                         {x, y + h, u0, v1, color}};
    push(tex, v);
}

void SpriteBatch::rotated(const Texture &tex, float cx, float cy, float w, float h, float angle, uint32_t color) {
    const float c = std::cos(angle), s = std::sin(angle);
    const float hw = w * 0.5f, hh = h * 0.5f;
    const float px[4] = {-hw, hw, hw, -hw}, py[4] = {-hh, -hh, hh, hh};
    const float u[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
    Vertex v[4];
    for (int i = 0; i < 4; ++i) v[i] = Vertex{cx + px[i] * c - py[i] * s, cy + px[i] * s + py[i] * c, u[i], vv[i], color};
    push(tex, v);
}

void SpriteBatch::quad4(const Texture &tex, const float xy[8], uint32_t color) {
    const Vertex v[4] = {{xy[0], xy[1], 0, 0, color}, {xy[2], xy[3], 1, 0, color}, {xy[4], xy[5], 1, 1, color},
                         {xy[6], xy[7], 0, 1, color}};
    push(tex, v);
}

void SpriteBatch::line(float x0, float y0, float x1, float y1, float t, uint32_t color) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    quad4(white_, xy, color);
}

void SpriteBatch::gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    const Vertex v[4] = {{x, y, 0, 0, top}, {x + w, y, 1, 0, top}, {x + w, y + h, 1, 1, bottom},
                         {x, y + h, 0, 1, bottom}};
    push(white_, v);
}

void SpriteBatch::setTarget(const RenderTarget *target) {
    flush();
    target_ = target;
    if (target) {
        glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        glViewport(0, 0, target->color.width, target->color.height);
        // Dans une texture, l'axe Y est inversé par rapport à l'écran.
        scaleY_ = 2.0f / virtualH_;
        offsetY_ = -1.0f;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, screenFbo_);
        glViewport(0, 0, viewportW_, viewportH_);
        scaleY_ = -2.0f / virtualH_;
        offsetY_ = 1.0f;
    }
}

void SpriteBatch::centered(const Texture &tex, float cx, float cy, float size, uint32_t color) {
    quad(tex, cx - size * 0.5f, cy - size * 0.5f, size, size, color);
}

void SpriteBatch::rect(float x, float y, float w, float h, uint32_t color) { quad(white_, x, y, w, h, color); }

void SpriteBatch::outline(float x, float y, float w, float h, float t, uint32_t color) {
    rect(x, y, w, t, color);
    rect(x, y + h - t, w, t, color);
    rect(x, y + t, t, h - 2 * t, color);
    rect(x + w - t, y + t, t, h - 2 * t, color);
}

void SpriteBatch::flush() {
    if (vertices_.empty()) return;
    glUseProgram(program_);
    glUniform2f(scaleLoc_, scaleX_, scaleY_);
    glUniform2f(offsetLoc_, -1.0f, offsetY_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const size_t bytes = vertices_.size() * sizeof(Vertex);
    // Ré-allocation complète ("orphaning") : le pilote n'a pas à attendre que le GPU
    // ait fini de lire l'image précédente.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), vertices_.data(), GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    Blend current = static_cast<Blend>(255);
    int currentClip = -2;
    for (const Command &cmd : commands_) {
        if (cmd.clip != currentClip) {
            currentClip = cmd.clip;
            if (cmd.clip < 0 || target_) {
                glDisable(GL_SCISSOR_TEST);
            } else {
                const Clip &c = clipTable_[static_cast<size_t>(cmd.clip)];
                const float kx = static_cast<float>(viewportW_) / virtualW_, ky = static_cast<float>(viewportH_) / virtualH_;
                glEnable(GL_SCISSOR_TEST);
                glScissor(static_cast<GLint>(std::floor(c.x * kx)),
                          static_cast<GLint>(std::floor(static_cast<float>(viewportH_) - (c.y + c.h) * ky)),
                          static_cast<GLsizei>(std::ceil(c.w * kx)), static_cast<GLsizei>(std::ceil(c.h * ky)));
            }
        }
        if (cmd.blend != current) {
            current = cmd.blend;
            glUniform1f(maskLoc_, maskWriting_ ? 1.0f : 0.0f);
            glUniform1f(premulLoc_, current == Blend::Premultiplied ? 1.0f : 0.0f);
            if (current == Blend::Additive)
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            else if (current == Blend::Premultiplied)
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            else
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        glBindTexture(GL_TEXTURE_2D, cmd.texture);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.quadCount * 6), GL_UNSIGNED_INT,
                       reinterpret_cast<void *>(static_cast<uintptr_t>(cmd.firstQuad) * 6 * sizeof(uint32_t)));
        current_.drawCalls++;
        current_.quads += static_cast<int>(cmd.quadCount);
    }
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    current_.flushes++;
    current_.bytesUploaded += bytes;
    vertices_.clear();
    commands_.clear();
}

void SpriteBatch::end() {
    flush();
    clipTable_.clear();
    last_ = current_;
}

}  // namespace gfx
