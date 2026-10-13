// SPDX-License-Identifier: GPL-3.0-or-later
// Étape 1 du plan : fenêtre, sprites, son, manette, logs. La scène est un petit
// banc d'essai rythmique : des cercles apparaissent sur un tempo fixe, calé sur
// l'horloge audio, et il faut les frapper au moment du "tic". On mesure l'écart
// moyen (décalage audio/vidéo/entrée ressenti), les FPS, la régularité audio, etc.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "core/fsprobe.h"
#include "platform/platform.h"
#include "render/batch.h"
#include "render/font.h"

class App {
public:
    bool init(const std::vector<fsprobe::Result> &probes);
    void frame(const plat::InputState &input, double now);
    void shutdown();
    bool wantsExit() const { return exitRequested_; }

    static constexpr float kVirtualW = 1920.0f;
    static constexpr float kVirtualH = 1080.0f;

private:
    struct Circle {
        long beat = 0;
        double time = 0;     // instant idéal (secondes, temps du morceau)
        float x = 0, y = 0;
        bool judged = false;
        int result = 0;      // 300, 100, 50, 0 = raté
        double judgedAt = 0;
        float errorMs = 0;
    };
    struct Popup {
        float x, y;
        double time;
        int result;
        float errorMs;
    };

    void update(const plat::InputState &in, double now, double dt);
    void handleHit(double songTime, double now);
    void scheduleBeats();
    void render(double now);
    void renderOverlay(double now);
    double songTime() const;
    void addError(float ms);

    bool pressed(uint32_t padBit) const { return (cur_.pad.buttons & padBit) && !(prev_.pad.buttons & padBit); }
    bool keyPressed(int hid) const { return cur_.keyboard.keys[hid] && !prev_.keyboard.keys[hid]; }

    gfx::SpriteBatch batch_;
    gfx::BitmapFont font_;
    gfx::Texture circleTex_, ringTex_, dotTex_;

    audio::Mixer mixer_;
    audio::Sample hitSound_, tickSound_, accentSound_;
    bool audioOk_ = false;
    uint64_t songStartFrame_ = 0;
    long nextBeatToSchedule_ = 0;
    bool metronome_ = true;
    double offsetMs_ = 0;    // compensation réglée par le joueur

    plat::InputState cur_, prev_;
    float cursorX_ = kVirtualW / 2, cursorY_ = kVirtualH / 2;
    float cursorSpeed_ = 1400.0f;  // pixels virtuels par seconde, stick à fond
    float lastMouseX_ = -1, lastMouseY_ = -1;
    const char *cursorSource_ = "stick";

    std::deque<Circle> circles_;
    std::deque<Popup> popups_;
    long nextBeatToSpawn_ = 0;
    std::deque<float> errors_;
    int count300_ = 0, count100_ = 0, count50_ = 0, countMiss_ = 0, countPresses_ = 0;

    bool overlay_ = true;
    bool stress_ = false;
    bool exitRequested_ = false;
    double exitHoldStart_ = -1;
    double crashHoldStart_ = -1;

    double lastFrame_ = -1;
    double frameTimes_[240] = {};
    int frameIndex_ = 0;
    long frameCount_ = 0;
    long hitches_ = 0;
    double nextStatsLog_ = 0;
    double clockStartWall_ = -1, clockStartAudio_ = 0;

    std::string glVersion_, glRenderer_, assetLine_;
    std::vector<fsprobe::Result> probes_;
};
