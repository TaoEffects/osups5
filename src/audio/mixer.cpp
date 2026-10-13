// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/mixer.h"

#include <cstring>

#include "platform/platform.h"

namespace audio {

void Mixer::callback(float *out, int frames, void *user) { static_cast<Mixer *>(user)->mix(out, frames); }

void Mixer::push(const Command &cmd) {
    const uint32_t head = queueHead_.load(std::memory_order_relaxed);
    const uint32_t tail = queueTail_.load(std::memory_order_acquire);
    if (head - tail >= kQueueSize) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    queue_[head % kQueueSize] = cmd;
    queueHead_.store(head + 1, std::memory_order_release);
}

void Mixer::play(const Sample *sample, float gain, float pan) { push(Command{sample, 0, gain, pan}); }

void Mixer::playAt(const Sample *sample, uint64_t frame, float gain, float pan) {
    push(Command{sample, frame == 0 ? 1 : frame, gain, pan});
}

void Mixer::mix(float *out, int frames) {
    const double now = plat::nowSeconds();
    const uint64_t blockStart = framesMixed_.load(std::memory_order_relaxed);
    const uint64_t blockEnd = blockStart + static_cast<uint64_t>(frames);

    // Statistiques de régularité du thread audio.
    if (lastCallbackTime_ >= 0) {
        const double interval = (now - lastCallbackTime_) * 1000.0;
        const double avg = avgInterval_.load(std::memory_order_relaxed);
        avgInterval_.store(avg == 0 ? interval : avg * 0.98 + interval * 0.02, std::memory_order_relaxed);
        if (resetMax_.exchange(false)) maxInterval_.store(0, std::memory_order_relaxed);
        if (interval > maxInterval_.load(std::memory_order_relaxed)) maxInterval_.store(interval, std::memory_order_relaxed);
    }
    lastCallbackTime_ = now;

    // Récupère les nouveaux ordres.
    uint32_t tail = queueTail_.load(std::memory_order_relaxed);
    const uint32_t head = queueHead_.load(std::memory_order_acquire);
    while (tail != head) {
        const Command &cmd = queue_[tail % kQueueSize];
        for (Voice &v : voices_) {
            if (v.sample) continue;
            v.sample = cmd.sample;
            v.startFrame = cmd.startFrame == 0 ? blockStart : cmd.startFrame;
            v.position = 0;
            const float pan = cmd.pan < -1 ? -1 : (cmd.pan > 1 ? 1 : cmd.pan);
            v.gainL = cmd.gain * (pan > 0 ? 1.0f - pan : 1.0f);
            v.gainR = cmd.gain * (pan < 0 ? 1.0f + pan : 1.0f);
            break;
        }
        ++tail;
    }
    queueTail_.store(tail, std::memory_order_release);

    std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
    const float master = master_.load(std::memory_order_relaxed);
    int active = 0;
    for (Voice &v : voices_) {
        if (!v.sample) continue;
        if (v.startFrame >= blockEnd) {  // programmé plus tard
            ++active;
            continue;
        }
        int offset = 0;
        if (v.startFrame > blockStart) offset = static_cast<int>(v.startFrame - blockStart);
        if (v.startFrame < blockStart && v.position == 0) {
            // Ordre arrivé en retard : on joue tout de suite plutôt que de sauter le début.
            v.startFrame = blockStart;
        }
        const float *src = v.sample->stereo.data();
        const int total = v.sample->frames();
        for (int i = offset; i < frames && v.position < total; ++i, ++v.position) {
            out[i * 2] += src[v.position * 2] * v.gainL * master;
            out[i * 2 + 1] += src[v.position * 2 + 1] * v.gainR * master;
        }
        if (v.position >= total) {
            v.sample = nullptr;
        } else {
            ++active;
        }
    }
    // Écrêtage doux pour éviter les claquements si plusieurs sons se superposent.
    for (int i = 0; i < frames * 2; ++i) {
        const float x = out[i];
        out[i] = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    }

    activeVoices_.store(active, std::memory_order_relaxed);
    callbacks_.fetch_add(1, std::memory_order_relaxed);

    // Publie l'instantané (début du bloc mixé, heure de l'appel).
    seq_.fetch_add(1, std::memory_order_acq_rel);  // impair : écriture en cours
    snapFrames_.store(blockStart, std::memory_order_relaxed);
    snapTime_.store(now, std::memory_order_relaxed);
    snapBlock_.store(frames, std::memory_order_relaxed);
    seq_.fetch_add(1, std::memory_order_acq_rel);  // pair : cohérent
    framesMixed_.store(blockEnd, std::memory_order_release);
}

double Mixer::clockSeconds() const {
    uint64_t frames = 0;
    double t = 0;
    int block = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        frames = snapFrames_.load(std::memory_order_relaxed);
        t = snapTime_.load(std::memory_order_relaxed);
        block = snapBlock_.load(std::memory_order_relaxed);
        if (seq_.load(std::memory_order_acquire) == s1) break;
    }
    if (block == 0) return 0;
    // Position = début du dernier bloc + temps écoulé depuis, borné à 2 blocs pour
    // ne pas dériver si le thread audio cale.
    double elapsed = plat::nowSeconds() - t;
    const double maxElapsed = 2.0 * block / kRate;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > maxElapsed) elapsed = maxElapsed;
    return static_cast<double>(frames) / kRate + elapsed;
}

MixerStats Mixer::stats() const {
    MixerStats s;
    s.callbacks = callbacks_.load(std::memory_order_relaxed);
    s.framesMixed = framesMixed_.load(std::memory_order_relaxed);
    s.lastBlockFrames = snapBlock_.load(std::memory_order_relaxed);
    s.activeVoices = activeVoices_.load(std::memory_order_relaxed);
    s.avgIntervalMs = avgInterval_.load(std::memory_order_relaxed);
    s.maxIntervalMs = maxInterval_.load(std::memory_order_relaxed);
    s.droppedCommands = dropped_.load(std::memory_order_relaxed);
    return s;
}

}  // namespace audio
