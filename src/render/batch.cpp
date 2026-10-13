// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/batch.h"

#include <cmath>

#include "core/log.h"

namespace gfx {
namespace {

const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScale;
uniform vec2 uOffset;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = vec4(aPos * uScale + uOffset, 0.0, 1.0);
}
)";

const char *kFragmentShader = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 oColor;
void main() {
    oColor = texture(uTex, vUV) * vColor;
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewportW, viewportH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void SpriteBatch::push(const Texture &tex, const Vertex v[4]) {
    if (vertices_.size() / 4 >= kMaxQuads) flush();
    const uint32_t quadIndex = static_cast<uint32_t>(vertices_.size() / 4);
    if (commands_.empty() || commands_.back().texture != tex.id || commands_.back().blend != blend_) {
        commands_.push_back(Command{tex.id, blend_, quadIndex, 0});
    }
    commands_.back().quadCount++;
    vertices_.insert(vertices_.end(), v, v + 4);
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
    if (target) {
        glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        glViewport(0, 0, target->color.width, target->color.height);
        // Dans une texture, l'axe Y est inversé par rapport à l'écran.
        scaleY_ = 2.0f / virtualH_;
        offsetY_ = -1.0f;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    for (const Command &cmd : commands_) {
        if (cmd.blend != current) {
            current = cmd.blend;
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
    current_.flushes++;
    current_.bytesUploaded += bytes;
    vertices_.clear();
    commands_.clear();
}

void SpriteBatch::end() {
    flush();
    last_ = current_;
}

}  // namespace gfx
