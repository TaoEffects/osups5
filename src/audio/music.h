// SPDX-License-Identifier: GPL-3.0-or-later
// Musique en flux : le fichier (MP3/OGG/WAV) est gardé compressé en mémoire, un
// thread le décode et le rééchantillonne à 48 kHz dans un tampon circulaire, le
// thread audio y puise. La position de lecture sert d'horloge au gameplay.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <pthread.h>

#include "audio/stretch.h"

namespace audio {

class Mixer;

// Source décodée à sa fréquence d'origine, en float stéréo entrelacé.
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual int read(float *stereo, int frames) = 0;  // 0 = fin
    virtual bool seek(uint64_t frame) = 0;
    int rate = 44100;
    uint64_t lengthFrames = 0;  // 0 si inconnue
};
std::unique_ptr<Decoder> openDecoder(std::shared_ptr<std::vector<uint8_t>> data, const std::string &nameHint);

// Décode entièrement un petit fichier (hitsound) à 48 kHz stéréo.
bool decodeWholeFile(const std::string &path, std::vector<float> &stereo48k);

class Music {
public:
    ~Music();
    bool load(const std::string &path);
    void unload();
    bool loaded() const { return decoder_ != nullptr; }
    const std::string &path() const { return path_; }

    // Démarre (ou reprend) la lecture à `seconds` (peut être négatif : silence d'abord).
    void play(double seconds);
    void pause();
    void resume();
    bool playing() const { return playing_.load(); }
    bool ended() const { return ended_.load(); }
    void setLoop(bool loop, double loopStartSeconds) {
        loop_.store(loop);
        loopStart_.store(loopStartSeconds);
    }
    // Vitesse de lecture (1.5 = Double Time). preservePitch : tempo seul (DT/HT),
    // sinon la hauteur suit (Nightcore). Prend effet tout de suite, sans saut audible.
    void setRate(double rate, bool preservePitch);
    double rate() const { return rate_.load(); }
    void setVolume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }
    double lengthSeconds() const;
    // Position entendue en secondes, interpolée entre deux blocs audio.
    double time(const Mixer &mixer) const;
    // Niveau sonore récent (0..1), pour les animations au rythme.
    float level() const { return level_.load(); }
    // Spectre grossier (64 bandes de ~40 Hz à ~12 kHz, 0..1), pour le visualiseur.
    static constexpr int kBands = 64;
    std::array<float, kBands> spectrum() const;

    // Appelé par le mixeur dans le thread audio.
    void render(float *out, int frames, uint64_t mixerFrame);

private:
    static void *decodeThread(void *self);
    void decodeLoop();

    std::shared_ptr<std::vector<uint8_t>> data_;
    std::unique_ptr<Decoder> decoder_;
    std::string path_;

    std::mutex mutex_;                 // protège le tampon et les ordres de saut
    std::vector<float> ring_;          // stéréo 48 kHz
    size_t ringRead_ = 0, ringCount_ = 0;
    int64_t ringStartPos_ = 0;         // position (trames 48 kHz) de la prochaine trame lue
    int64_t pendingSeek_ = INT64_MIN;  // demande de saut (trames 48 kHz)
    bool decoderEof_ = false;

    std::atomic<bool> playing_{false}, ended_{false}, loop_{false}, threadRun_{false};
    std::atomic<double> loopStart_{0};
    std::atomic<float> volume_{1.0f}, level_{0};
    std::array<std::atomic<float>, kBands> bands_{};
    std::vector<float> history_ = std::vector<float>(1024, 0.0f);  // derniers échantillons mono
    size_t historyPos_ = 0;
    void analyse();
    // Vitesse demandée, et vitesse réellement appliquée au contenu du tampon
    // (changée par le thread de décodage lors d'un saut, sous mutex_).
    std::atomic<double> rate_{1.0};
    std::atomic<bool> preservePitch_{true};
    double ringRate_ = 1.0;
    std::atomic<double> playPos_{0};  // position courante dans le morceau (trames 48 kHz)
    // Instantané pour l'horloge : position au début du dernier bloc et trame mixeur.
    std::atomic<double> snapPos_{0}, snapRate_{1.0};
    std::atomic<uint64_t> snapMixerFrame_{0};
    std::atomic<bool> snapValid_{false};
    pthread_t thread_{};
};

}  // namespace audio
