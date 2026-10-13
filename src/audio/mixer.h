// SPDX-License-Identifier: GPL-3.0-or-later
// Mixeur logiciel 48 kHz stéréo float, sans BASS ni dépendance externe.
//  - le thread principal envoie des ordres "jouer ce son" via une file sans verrou ;
//  - le thread audio (plateforme) appelle Mixer::callback par blocs de N trames ;
//  - les sons peuvent être programmés à la trame près (playAt) : c'est ce qui
//    permettra plus tard de caler les hitsounds sur la musique ;
//  - clockSeconds() donne une horloge audio interpolée entre deux blocs, base du
//    calcul de position de la musique (comme le PlaybackInterpolator de McOsu).
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace audio {

struct Sample {
    std::vector<float> stereo;  // L, R entrelacés, 48 kHz
    int frames() const { return static_cast<int>(stereo.size() / 2); }
};

struct MixerStats {
    uint64_t callbacks = 0;
    uint64_t framesMixed = 0;
    int lastBlockFrames = 0;
    int activeVoices = 0;
    double avgIntervalMs = 0;   // intervalle moyen entre deux appels du thread audio
    double maxIntervalMs = 0;   // pire intervalle depuis la dernière remise à zéro
    uint64_t droppedCommands = 0;
};

class Mixer {
public:
    static constexpr int kRate = 48000;

    // Fonction à passer à plat::audioStart (user = this).
    static void callback(float *out, int frames, void *user);

    // Thread principal uniquement (une seule source d'ordres).
    void play(const Sample *sample, float gain, float pan = 0.0f);
    void playAt(const Sample *sample, uint64_t frame, float gain, float pan = 0.0f);
    void setMasterGain(float gain) { master_.store(gain); }

    // Nombre de trames déjà mixées (position "écrite" de la ligne de temps audio).
    uint64_t framesMixed() const { return framesMixed_.load(std::memory_order_acquire); }
    // Horloge audio en secondes, interpolée avec l'horloge murale entre deux blocs.
    double clockSeconds() const;

    MixerStats stats() const;
    void resetIntervalStats() { resetMax_.store(true); }

private:
    struct Command {
        const Sample *sample;
        uint64_t startFrame;  // 0 = dès que possible
        float gain;
        float pan;
    };
    struct Voice {
        const Sample *sample = nullptr;
        uint64_t startFrame = 0;
        int position = 0;
        float gainL = 0, gainR = 0;
    };

    void push(const Command &cmd);
    void mix(float *out, int frames);

    static constexpr int kQueueSize = 256;
    static constexpr int kMaxVoices = 48;

    Command queue_[kQueueSize];
    std::atomic<uint32_t> queueHead_{0};  // écrit par le thread principal
    std::atomic<uint32_t> queueTail_{0};  // écrit par le thread audio
    Voice voices_[kMaxVoices];
    std::atomic<float> master_{0.8f};

    std::atomic<uint64_t> framesMixed_{0};
    // Instantané (trames, heure murale) publié par le thread audio, protégé par
    // un compteur de séquence (seqlock) pour une lecture cohérente sans verrou.
    std::atomic<uint32_t> seq_{0};
    std::atomic<uint64_t> snapFrames_{0};
    std::atomic<double> snapTime_{0};
    std::atomic<int> snapBlock_{0};

    std::atomic<uint64_t> callbacks_{0};
    std::atomic<int> activeVoices_{0};
    std::atomic<double> avgInterval_{0}, maxInterval_{0};
    std::atomic<bool> resetMax_{false};
    std::atomic<uint64_t> dropped_{0};
    double lastCallbackTime_ = -1;  // thread audio uniquement
};

}  // namespace audio
