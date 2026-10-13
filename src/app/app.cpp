// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/app.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio/synth.h"
#include "core/log.h"
#include "core/netlog.h"
#include "render/gl.h"
#include "render/procedural.h"
#include "version.h"

namespace {

// Aire de jeu : 512x384 "pixels osu!" mis à l'échelle 1.875 et centrés.
constexpr float kFieldW = 960.0f, kFieldH = 720.0f;
constexpr float kFieldX = (App::kVirtualW - kFieldW) / 2.0f;
constexpr float kFieldY = (App::kVirtualH - kFieldH) / 2.0f + 30.0f;
constexpr float kCircleRadius = 90.0f;

constexpr double kBpm = 120.0;
constexpr double kBeatLen = 60.0 / kBpm;
constexpr double kApproach = 0.9;    // durée du cercle d'approche (s)
constexpr double kWindow50 = 0.150;  // fenêtres de jugement (s)
constexpr double kWindow100 = 0.100;
constexpr double kWindow300 = 0.050;
constexpr double kLookahead = 0.25;  // programmation audio en avance (s)

// Codes d'usage HID du clavier.
constexpr int kKeyZ = 0x1D, kKeyX = 0x1B, kKeyEsc = 0x29, kKeyF1 = 0x3A, kKeyTab = 0x2B, kKeyT = 0x17,
              kKeyM = 0x10, kKeyR = 0x15, kKeyA = 0x04, kKeyUp = 0x52, kKeyDown = 0x51, kKeyLeft = 0x50,
              kKeyRight = 0x4F, kKeyF12 = 0x45;

const uint32_t kComboColors[] = {gfx::rgba(255, 102, 170), gfx::rgba(102, 204, 255), gfx::rgba(255, 204, 102),
                                 gfx::rgba(153, 255, 153)};

bool g_requireAim = false;  // mode "rythme seul" par défaut : on mesure la latence

uint32_t hashBeat(long beat) {
    uint32_t h = static_cast<uint32_t>(beat) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

float deadzone(float v, float dz) {
    const float a = std::fabs(v);
    if (a <= dz) return 0;
    const float n = (a - dz) / (1.0f - dz);
    return std::copysign(std::pow(n, 1.6f), v);
}

}  // namespace

bool App::init(const std::vector<fsprobe::Result> &probes) {
    probes_ = probes;
    if (const GLubyte *v = glGetString(GL_VERSION)) glVersion_ = reinterpret_cast<const char *>(v);
    if (const GLubyte *r = glGetString(GL_RENDERER)) glRenderer_ = reinterpret_cast<const char *>(r);
    LOG("gl: version=%s renderer=%s", glVersion_.c_str(), glRenderer_.c_str());

    if (!batch_.init()) return false;
    if (!font_.init()) return false;
    circleTex_ = gfx::makeHitCircle(256);
    ringTex_ = gfx::makeRing(256, 0.06f);
    dotTex_ = gfx::makeSoftDot(64);
    LOG("app: textures generees (gl err 0x%x)", glGetError());

    // Lecture d'un fichier embarqué : valide l'accès aux données de l'appli.
    char path[256];
    snprintf(path, sizeof(path), "%s/info.txt", plat::assetsDir());
    if (FILE *f = fopen(path, "r")) {
        char line[160] = {};
        if (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
            assetLine_ = line;
        }
        fclose(f);
        LOG("app: %s lu: \"%s\"", path, assetLine_.c_str());
    } else {
        assetLine_ = "(asset illisible)";
        LOG("app: impossible de lire %s", path);
    }

    hitSound_ = audio::makeHitSound();
    tickSound_ = audio::makeTick(1500.0f);
    accentSound_ = audio::makeTick(2200.0f);
    audioOk_ = plat::audioStart(audio::Mixer::callback, &mixer_, 256);
    if (!audioOk_) LOG("app: AUDIO INDISPONIBLE, horloge murale utilisee a la place");
    // Le "morceau" commence une seconde après le démarrage de l'audio.
    songStartFrame_ = mixer_.framesMixed() + audio::Mixer::kRate;
    clockStartWall_ = -1;
    return true;
}

void App::shutdown() {
    plat::audioStop();
    gfx::destroyTexture(circleTex_);
    gfx::destroyTexture(ringTex_);
    gfx::destroyTexture(dotTex_);
    font_.shutdown();
    batch_.shutdown();
}

double App::songTime() const {
    if (audioOk_) {
        return mixer_.clockSeconds() - static_cast<double>(songStartFrame_) / audio::Mixer::kRate;
    }
    return plat::nowSeconds() - clockStartWall_ - 1.0;
}

void App::addError(float ms) {
    errors_.push_back(ms);
    while (errors_.size() > 30) errors_.pop_front();
}

void App::scheduleBeats() {
    if (!audioOk_) return;
    const double horizon = songTime() + kLookahead;
    while (nextBeatToSchedule_ * kBeatLen < horizon) {
        const long beat = nextBeatToSchedule_++;
        if (beat < 0 || !metronome_) continue;
        const uint64_t frame =
            songStartFrame_ + static_cast<uint64_t>(std::llround(beat * kBeatLen * audio::Mixer::kRate));
        mixer_.playAt(beat % 4 == 0 ? &accentSound_ : &tickSound_, frame, 0.9f);
    }
}

void App::handleHit(double gameTime, double now) {
    for (Circle &c : circles_) {
        if (c.judged) continue;
        const double diff = gameTime - c.time;
        if (diff < -kWindow50) break;  // trop tôt pour tous les cercles suivants
        if (diff > kWindow50) continue;
        if (g_requireAim) {
            const float dx = cursorX_ - c.x, dy = cursorY_ - c.y;
            if (dx * dx + dy * dy > kCircleRadius * kCircleRadius) return;  // clic à côté
        }
        const double a = std::fabs(diff);
        c.judged = true;
        c.judgedAt = now;
        c.errorMs = static_cast<float>(diff * 1000.0);
        c.result = a <= kWindow300 ? 300 : (a <= kWindow100 ? 100 : 50);
        (c.result == 300 ? count300_ : (c.result == 100 ? count100_ : count50_))++;
        addError(c.errorMs);
        popups_.push_back(Popup{c.x, c.y - kCircleRadius, now, c.result, c.errorMs});
        return;
    }
}

void App::update(const plat::InputState &in, double now, double dt) {
    prev_ = cur_;
    cur_ = in;
    const plat::PadState &pad = cur_.pad;

    // --- Raccourcis ---------------------------------------------------------------
    const bool l1r1 = (pad.buttons & plat::kPadL1) && (pad.buttons & plat::kPadR1);
    if ((pressed(plat::kPadOptions) && !l1r1) || keyPressed(kKeyF1) || keyPressed(kKeyTab)) overlay_ = !overlay_;
    if (pressed(plat::kPadTriangle) || keyPressed(kKeyT)) {
        stress_ = !stress_;
        LOG("app: test de charge %s", stress_ ? "active (5000 sprites)" : "desactive");
    }
    if (pressed(plat::kPadL3) || keyPressed(kKeyM)) {
        metronome_ = !metronome_;
        LOG("app: metronome %s", metronome_ ? "on" : "off");
    }
    if (pressed(plat::kPadR3) || keyPressed(kKeyA)) {
        g_requireAim = !g_requireAim;
        LOG("app: visee %s", g_requireAim ? "requise" : "ignoree (rythme seul)");
    }
    if (pressed(plat::kPadCircle) || keyPressed(kKeyR)) {
        errors_.clear();
        count300_ = count100_ = count50_ = countMiss_ = countPresses_ = 0;
        mixer_.resetIntervalStats();
        hitches_ = 0;
        LOG("app: statistiques remises a zero");
    }
    if (pressed(plat::kPadUp) || keyPressed(kKeyUp)) offsetMs_ += 5;
    if (pressed(plat::kPadDown) || keyPressed(kKeyDown)) offsetMs_ -= 5;
    if (pressed(plat::kPadUp) || pressed(plat::kPadDown) || keyPressed(kKeyUp) || keyPressed(kKeyDown))
        LOG("app: decalage = %+.0f ms", offsetMs_);
    if (pressed(plat::kPadRight) || keyPressed(kKeyRight)) cursorSpeed_ += 100;
    if ((pressed(plat::kPadLeft) || keyPressed(kKeyLeft)) && cursorSpeed_ > 200) cursorSpeed_ -= 100;

    // Quitter : L1+R1+Options maintenus 2 s (Échap sur PC).
    if (l1r1 && (pad.buttons & plat::kPadOptions)) {
        if (exitHoldStart_ < 0) exitHoldStart_ = now;
        if (now - exitHoldStart_ > 2.0) exitRequested_ = true;
    } else {
        exitHoldStart_ = -1;
    }
    if (keyPressed(kKeyEsc) || cur_.quitRequested) exitRequested_ = true;

    // Plantage volontaire : L3+R3 maintenus 3 s (ou F12 sur PC). Sert à vérifier
    // que le rapport de plantage arrive bien jusqu'au PC.
    if (((pad.buttons & plat::kPadL3) && (pad.buttons & plat::kPadR3)) || cur_.keyboard.keys[kKeyF12]) {
        if (crashHoldStart_ < 0) crashHoldStart_ = now;
        if (now - crashHoldStart_ > 3.0 || keyPressed(kKeyF12)) {
            LOG("app: plantage volontaire demande");
            volatile int *nowhere = nullptr;
            *nowhere = 42;
        }
    } else {
        crashHoldStart_ = -1;
    }

    // --- Curseur ------------------------------------------------------------------
    const plat::MouseState &mouse = cur_.mouse;
    if (mouse.connected && (mouse.x != lastMouseX_ || mouse.y != lastMouseY_)) {
        if (lastMouseX_ >= 0) {
            cursorX_ = mouse.x * kVirtualW / static_cast<float>(plat::width() > 0 ? plat::width() : 1);
            cursorY_ = mouse.y * kVirtualH / static_cast<float>(plat::height() > 0 ? plat::height() : 1);
            cursorSource_ = "souris";
        }
        lastMouseX_ = mouse.x;
        lastMouseY_ = mouse.y;
    }
    if (pad.touchCount > 0) {
        // Pavé tactile en absolu, façon zone de tablette : tout le pavé -> l'aire de jeu.
        cursorX_ = kFieldX + pad.touchX[0] / 1920.0f * kFieldW;
        cursorY_ = kFieldY + pad.touchY[0] / 1080.0f * kFieldH;
        cursorSource_ = "pave tactile";
    } else {
        const float vx = deadzone(pad.lx, 0.12f) + 0.3f * deadzone(pad.rx, 0.12f);
        const float vy = deadzone(pad.ly, 0.12f) + 0.3f * deadzone(pad.ry, 0.12f);
        if (vx != 0 || vy != 0) {
            cursorX_ += vx * cursorSpeed_ * static_cast<float>(dt);
            cursorY_ += vy * cursorSpeed_ * static_cast<float>(dt);
            cursorSource_ = "stick";
        }
    }
    cursorX_ = cursorX_ < 0 ? 0 : (cursorX_ > kVirtualW ? kVirtualW : cursorX_);
    cursorY_ = cursorY_ < 0 ? 0 : (cursorY_ > kVirtualH ? kVirtualH : cursorY_);

    // --- Rythme -------------------------------------------------------------------
    scheduleBeats();
    const double gameTime = songTime() - offsetMs_ / 1000.0;
    while ((nextBeatToSpawn_ * kBeatLen) - kApproach < gameTime) {
        const long beat = nextBeatToSpawn_++;
        if (beat < 0) continue;
        const uint32_t h = hashBeat(beat);
        Circle c;
        c.beat = beat;
        c.time = beat * kBeatLen;
        c.x = kFieldX + kCircleRadius + static_cast<float>(h % 1000) / 1000.0f * (kFieldW - 2 * kCircleRadius);
        c.y = kFieldY + kCircleRadius + static_cast<float>((h / 1000) % 1000) / 1000.0f * (kFieldH - 2 * kCircleRadius);
        circles_.push_back(c);
    }

    const uint32_t hitPad = plat::kPadCross | plat::kPadSquare | plat::kPadL1 | plat::kPadR1 | plat::kPadL2 |
                            plat::kPadR2;
    const uint32_t padEdges = cur_.pad.buttons & ~prev_.pad.buttons & hitPad;
    int presses = 0;
    for (uint32_t b = padEdges; b; b &= b - 1) ++presses;
    if (!l1r1) {
        presses += keyPressed(kKeyZ) + keyPressed(kKeyX);
        presses += ((mouse.buttons & 1u) && !(prev_.mouse.buttons & 1u)) + ((mouse.buttons & 2u) && !(prev_.mouse.buttons & 2u));
    } else {
        presses = 0;  // L1+R1 = début du raccourci pour quitter, pas une frappe
    }
    for (int i = 0; i < presses; ++i) {
        ++countPresses_;
        mixer_.play(&hitSound_, 0.8f);
        handleHit(gameTime, now);
    }

    for (Circle &c : circles_) {
        if (!c.judged && gameTime - c.time > kWindow50) {
            c.judged = true;
            c.judgedAt = now;
            c.result = 0;
            ++countMiss_;
            popups_.push_back(Popup{c.x, c.y - kCircleRadius, now, 0, 0});
        }
    }
    while (!circles_.empty() && circles_.front().judged && now - circles_.front().judgedAt > 0.3) circles_.pop_front();
    while (!popups_.empty() && now - popups_.front().time > 0.7) popups_.pop_front();
}

void App::frame(const plat::InputState &input, double now) {
    if (clockStartWall_ < 0) {
        clockStartWall_ = now;
        clockStartAudio_ = mixer_.clockSeconds();
        nextStatsLog_ = now + 5.0;
    }
    const double dt = lastFrame_ < 0 ? 1.0 / 60.0 : now - lastFrame_;
    lastFrame_ = now;
    frameTimes_[frameIndex_] = dt;
    frameIndex_ = (frameIndex_ + 1) % 240;
    ++frameCount_;
    const int hz = plat::refreshHz() > 0 ? plat::refreshHz() : 60;
    if (frameCount_ > 10 && dt > 1.5 / hz) ++hitches_;

    update(input, now, dt);
    render(now);

    if (now >= nextStatsLog_) {
        nextStatsLog_ = now + 5.0;
        double sum = 0, worst = 0;
        for (double t : frameTimes_) {
            sum += t;
            worst = t > worst ? t : worst;
        }
        const audio::MixerStats ms = mixer_.stats();
        const double drift = ((mixer_.clockSeconds() - clockStartAudio_) - (now - clockStartWall_)) * 1000.0;
        LOG("stats: fps=%.1f max=%.1fms saccades=%ld dc=%d quads=%d | audio appels=%llu int=%.2f/%.2fms voix=%d "
            "derive=%+.1fms | frappes=%d 300=%d 100=%d 50=%d rate=%d",
            240.0 / sum, worst * 1000.0, hitches_, batch_.lastFrame().drawCalls, batch_.lastFrame().quads,
            static_cast<unsigned long long>(ms.callbacks), ms.avgIntervalMs, ms.maxIntervalMs, ms.activeVoices, drift,
            countPresses_, count300_, count100_, count50_, countMiss_);
    }
}

void App::render(double now) {
    glClearColor(0.07f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    batch_.begin(kVirtualW, kVirtualH, plat::width(), plat::height());

    const double gameTime = songTime() - offsetMs_ / 1000.0;

    // Aire de jeu.
    batch_.outline(kFieldX, kFieldY, kFieldW, kFieldH, 2, gfx::rgba(255, 255, 255, 40));

    // Flash de tempo : carré qui s'allume sur chaque temps (vérification image/son).
    const double phase = gameTime / kBeatLen - std::floor(gameTime / kBeatLen);
    const float flash = static_cast<float>(phase < 0.1 ? 1.0 - phase / 0.1 : 0.0);
    batch_.rect(kVirtualW / 2 - 30, 20, 60, 60, gfx::withAlpha(gfx::rgba(255, 255, 255), 0.1f + 0.9f * flash));

    // Test de charge : beaucoup de sprites, qui doivent rester en 1 ou 2 appels de dessin.
    if (stress_) {
        for (int i = 0; i < 5000; ++i) {
            const float t = static_cast<float>(now) * 0.5f + static_cast<float>(i) * 0.013f;
            const float x = kVirtualW / 2 + std::sin(t * 1.3f + i) * 900.0f;
            const float y = kVirtualH / 2 + std::cos(t * 1.7f + i * 0.7f) * 500.0f;
            batch_.centered(dotTex_, x, y, 24, kComboColors[i % 4]);
        }
    }

    // Cercles : les plus récents dessous, donc parcours à l'envers.
    for (auto it = circles_.rbegin(); it != circles_.rend(); ++it) {
        const Circle &c = *it;
        const uint32_t color = kComboColors[(c.beat / 4) % 4];
        if (c.judged) {
            if (c.result == 0) continue;
            const float k = static_cast<float>((now - c.judgedAt) / 0.3);
            batch_.centered(circleTex_, c.x, c.y, kCircleRadius * 2 * (1.0f + 0.4f * k), gfx::withAlpha(color, 1 - k));
            continue;
        }
        const double progress = (gameTime - (c.time - kApproach)) / kApproach;  // 0 -> 1 à l'instant idéal
        const float alpha = static_cast<float>(progress < 0.3 ? progress / 0.3 : 1.0);
        batch_.centered(circleTex_, c.x, c.y, kCircleRadius * 2, gfx::withAlpha(color, alpha));
        char num[4];
        snprintf(num, sizeof(num), "%ld", c.beat % 4 + 1);
        font_.draw(batch_, c.x - 12, c.y - 16, 4, gfx::withAlpha(gfx::rgba(255, 255, 255), alpha), num);
        if (progress < 1.0) {
            const float scale = 1.0f + 2.0f * static_cast<float>(1.0 - progress);
            batch_.centered(ringTex_, c.x, c.y, kCircleRadius * 2 * scale, gfx::withAlpha(color, alpha));
        }
    }

    for (const Popup &p : popups_) {
        const float k = static_cast<float>((now - p.time) / 0.7);
        char text[48];
        uint32_t color = gfx::rgba(255, 80, 80);
        if (p.result == 0) {
            snprintf(text, sizeof(text), "raté");
        } else {
            snprintf(text, sizeof(text), "%d %+.0fms", p.result, p.errorMs);
            color = p.result == 300 ? gfx::rgba(120, 200, 255) : (p.result == 100 ? gfx::rgba(120, 255, 120) : gfx::rgba(255, 220, 120));
        }
        const float w = gfx::BitmapFont::width(2, text);
        font_.drawShadow(batch_, p.x - w / 2, p.y - 20 - k * 30, 2, gfx::withAlpha(color, 1 - k), text);
    }

    batch_.centered(dotTex_, cursorX_, cursorY_, 56, gfx::rgba(255, 220, 240));

    // --- Informations permanentes --------------------------------------------------
    char line[256];
    float y = 20;
    font_.drawShadow(batch_, 20, y, 3, gfx::rgba(255, 255, 255), RHYTHM_APP_NAME " " RHYTHM_VERSION);
    y += 34;
    font_.drawShadow(batch_, 20, y, 2, gfx::rgba(180, 180, 200), "Étape 1 : fenêtre, sprites, son, manette");
    y += 30;

    double sum = 0;
    for (double t : frameTimes_) sum += t;
    snprintf(line, sizeof(line), "%.0f FPS   décalage %+.0f ms   métronome %s   visée %s", 240.0 / sum, offsetMs_,
             metronome_ ? "on" : "off", g_requireAim ? "requise" : "ignorée");
    font_.drawShadow(batch_, 20, y, 2, gfx::rgba(255, 255, 255), line);
    y += 24;
    float avg = 0, var = 0;
    for (float e : errors_) avg += e;
    if (!errors_.empty()) avg /= static_cast<float>(errors_.size());
    for (float e : errors_) var += (e - avg) * (e - avg);
    const float sd = errors_.size() > 1 ? std::sqrt(var / static_cast<float>(errors_.size() - 1)) : 0;
    snprintf(line, sizeof(line), "300:%d 100:%d 50:%d raté:%d   écart moyen %+.1f ms (écart-type %.1f, n=%zu)", count300_,
             count100_, count50_, countMiss_, avg, sd, errors_.size());
    font_.drawShadow(batch_, 20, y, 2, gfx::rgba(255, 255, 255), line);
    y += 24;
    if (errors_.size() >= 8) {
        snprintf(line, sizeof(line), "-> décalage conseillé : %+.0f ms", offsetMs_ + avg);
        font_.drawShadow(batch_, 20, y, 2, gfx::rgba(255, 220, 120), line);
    }
    snprintf(line, sizeof(line), "Log réseau : python3 tools/logrecv.py %s   (%s)", netlog::localAddress(),
             netlog::clientConnected() ? "PC connecté" : "en attente");
    font_.drawShadow(batch_, 20, kVirtualH - 92, 2, gfx::rgba(150, 220, 150), line);
    font_.drawShadow(batch_, 20, kVirtualH - 68, 2, gfx::rgba(200, 200, 200),
                     "Taper : Croix/Carré/L1/R1/L2/R2 (Z/X)   Viser : sticks, pavé tactile   Options : debug");
    font_.drawShadow(batch_, 20, kVirtualH - 44, 2, gfx::rgba(200, 200, 200),
                     "Haut/Bas : décalage   Triangle : charge   L3 : métronome   R3 : visée   Rond : RAZ   "
                     "L1+R1+Options 2 s : quitter");
    if (exitHoldStart_ >= 0) {
        snprintf(line, sizeof(line), "Fermeture dans %.1f s...", 2.0 - (now - exitHoldStart_));
        font_.drawShadow(batch_, kVirtualW / 2 - 200, kVirtualH / 2, 3, gfx::rgba(255, 120, 120), line);
    }

    if (overlay_) renderOverlay(now);
    batch_.end();
}

void App::renderOverlay(double now) {
    const float x = kVirtualW - 700, w = 690;
    batch_.rect(x - 10, 10, w + 10, kVirtualH - 130, gfx::rgba(0, 0, 0, 170));
    char line[256];
    float y = 20;
    const float lh = 21;
    auto put = [&](uint32_t color) {
        // Tronque pour rester dans le panneau (glyphes de 16 px).
        const size_t maxChars = static_cast<size_t>(w / 16);
        if (strlen(line) > maxChars) line[maxChars] = '\0';
        font_.draw(batch_, x, y, 2, color, line);
        y += lh;
    };
    const uint32_t title = gfx::rgba(255, 200, 120), text = gfx::rgba(230, 230, 230);

    snprintf(line, sizeof(line), "DEBUG (%s, %dx%d @ %d Hz)", plat::name(), plat::width(), plat::height(),
             plat::refreshHz());
    put(title);
    snprintf(line, sizeof(line), "GL %s", glVersion_.c_str());
    put(text);
    snprintf(line, sizeof(line), "%s", glRenderer_.c_str());
    put(text);

    double sum = 0, worst = 0;
    for (double t : frameTimes_) {
        sum += t;
        worst = t > worst ? t : worst;
    }
    const gfx::BatchStats &bs = batch_.lastFrame();
    snprintf(line, sizeof(line), "image moy %.2f ms  max %.2f ms", sum / 240 * 1000, worst * 1000);
    put(text);
    snprintf(line, sizeof(line), "saccades %ld  images %ld", hitches_, frameCount_);
    put(text);
    snprintf(line, sizeof(line), "draw calls %d  quads %d  envois %d", bs.drawCalls, bs.quads, bs.flushes);
    put(text);
    snprintf(line, sizeof(line), "sommets envoyés %.1f Ko/image", static_cast<double>(bs.bytesUploaded) / 1024.0);
    put(text);

    y += 6;
    const audio::MixerStats ms = mixer_.stats();
    snprintf(line, sizeof(line), "AUDIO %s, blocs %d trames", audioOk_ ? "ok" : "ABSENT", plat::audioBlockFrames());
    put(title);
    snprintf(line, sizeof(line), "appels %llu  intervalle moy %.2f max %.2f ms", static_cast<unsigned long long>(ms.callbacks),
             ms.avgIntervalMs, ms.maxIntervalMs);
    put(text);
    const double drift = ((mixer_.clockSeconds() - clockStartAudio_) - (now - clockStartWall_)) * 1000.0;
    snprintf(line, sizeof(line), "voix %d  ordres perdus %llu  dérive %+.1f ms", ms.activeVoices,
             static_cast<unsigned long long>(ms.droppedCommands), drift);
    put(text);

    y += 6;
    const plat::PadState &p = cur_.pad;
    snprintf(line, sizeof(line), "MANETTE %s  brut 0x%08x", p.connected ? "connectée" : "absente", p.rawButtons);
    put(title);
    snprintf(line, sizeof(line), "G %+.2f %+.2f  D %+.2f %+.2f  L2 %.2f R2 %.2f", p.lx, p.ly, p.rx, p.ry, p.l2, p.r2);
    put(text);
    snprintf(line, sizeof(line), "tactile %d doigt(s)  %.0f,%.0f  %.0f,%.0f", p.touchCount, p.touchX[0], p.touchY[0],
             p.touchX[1], p.touchY[1]);
    put(text);
    snprintf(line, sizeof(line), "curseur %s  vitesse %.0f", cursorSource_, cursorSpeed_);
    put(text);
    {
        int n = snprintf(line, sizeof(line), "CLAVIER %s :", cur_.keyboard.connected ? "oui" : "non");
        for (int k = 1; k < 256 && n < 200; ++k)
            if (cur_.keyboard.keys[k]) n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " %02X", k);
    }
    put(title);
    snprintf(line, sizeof(line), "SOURIS %s  %.0f,%.0f  boutons %u", cur_.mouse.connected ? "oui" : "non",
             cur_.mouse.x, cur_.mouse.y, cur_.mouse.buttons);
    put(title);

    y += 6;
    snprintf(line, sizeof(line), "RÉSEAU %s:%u  %s", netlog::localAddress(), netlog::port(),
             netlog::clientConnected() ? "PC connecté" : "pas de client");
    put(title);
    snprintf(line, sizeof(line), "asset : %s", assetLine_.c_str());
    put(text);
    snprintf(line, sizeof(line), "log : %s", logx::filePath());
    put(text);
    for (const fsprobe::Result &r : probes_) {
        snprintf(line, sizeof(line), "%-14s %s%s%s", r.path.c_str(), r.exists ? "existe " : "absent ",
                 r.listable ? "lisible " : "", r.writable ? "inscriptible" : "");
        put(r.writable ? gfx::rgba(150, 255, 150) : (r.exists ? text : gfx::rgba(140, 140, 140)));
    }

    y += 6;
    snprintf(line, sizeof(line), "DERNIERS LOGS");
    put(title);
    const float bottom = kVirtualH - 130;
    const size_t room = y < bottom ? static_cast<size_t>((bottom - y) / lh) : 0;
    for (const std::string &l : logx::recent(room)) {
        snprintf(line, sizeof(line), "%s", l.c_str() + (l.size() > 12 ? 12 : 0));  // sans l'horodatage
        put(gfx::rgba(170, 170, 170));
    }
}
