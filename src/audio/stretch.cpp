// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/stretch.h"

#include <algorithm>
#include <cmath>

namespace audio {

void Stretcher::reset(double rate) {
    rate_ = rate;
    in_.clear();
    mono_.clear();
    aPos_ = 0;
    prev_ = -1;
    std::fill(olap_.begin(), olap_.end(), 0.0f);
    out_.clear();
    outRead_ = 0;
    if (window_.empty()) {
        window_.resize(kWindow);
        // Hann périodique : deux fenêtres décalées d'un demi-pas somment à 1.
        for (int i = 0; i < kWindow; ++i)
            window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(6.28318530718f * static_cast<float>(i) / kWindow);
    }
}

void Stretcher::push(const float *stereo, int frames) {
    in_.insert(in_.end(), stereo, stereo + frames * 2);
    for (int i = 0; i < frames; ++i) mono_.push_back(stereo[i * 2] + stereo[i * 2 + 1]);
    process();
}

void Stretcher::finish() {
    const std::vector<float> silence(static_cast<size_t>(kWindow + kTolerance + kCorr) * 2, 0.0f);
    push(silence.data(), kWindow + kTolerance + kCorr);
}

int Stretcher::pull(float *stereo, int frames) {
    const int n = std::min(frames, available());
    std::copy(out_.begin() + static_cast<long>(outRead_ * 2), out_.begin() + static_cast<long>((outRead_ + n) * 2), stereo);
    outRead_ += static_cast<size_t>(n);
    if (outRead_ > 8192) {
        out_.erase(out_.begin(), out_.begin() + static_cast<long>(outRead_ * 2));
        outRead_ = 0;
    }
    return n;
}

void Stretcher::process() {
    while (hop()) {
    }
    // Oublie le début du tampon d'entrée devenu inutile.
    const int keep = std::min(static_cast<int>(aPos_) - kTolerance, prev_ < 0 ? 1 << 30 : prev_ + kHop);
    if (keep > 16384) {
        in_.erase(in_.begin(), in_.begin() + keep * 2);
        mono_.erase(mono_.begin(), mono_.begin() + keep);
        aPos_ -= keep;
        prev_ -= keep;
    }
}

bool Stretcher::hop() {
    const int frames = static_cast<int>(mono_.size());
    const int base = static_cast<int>(aPos_);
    if (base + kTolerance + kWindow > frames) return false;
    if (prev_ >= 0 && prev_ + kHop + kCorr > frames) return false;

    int start = base;
    if (prev_ >= 0) {
        // La suite naturelle du segment précédent sert de référence.
        const float *ref = &mono_[static_cast<size_t>(prev_ + kHop)];
        float best = -1e30f;
        const int lo = std::max(-kTolerance, -base);
        for (int k = lo; k <= kTolerance; k += 2) {
            const float *cand = &mono_[static_cast<size_t>(base + k)];
            float dot = 0, energy = 1e-6f;
            for (int i = 0; i < kCorr; i += 4) {
                dot += ref[i] * cand[i];
                energy += cand[i] * cand[i];
            }
            const float score = dot / std::sqrt(energy);
            if (score > best) {
                best = score;
                start = base + k;
            }
        }
    }
    const float *src = &in_[static_cast<size_t>(start) * 2];
    for (int i = 0; i < kWindow; ++i) {
        const float w = window_[static_cast<size_t>(i)];
        olap_[static_cast<size_t>(i) * 2] += src[i * 2] * w;
        olap_[static_cast<size_t>(i) * 2 + 1] += src[i * 2 + 1] * w;
    }
    out_.insert(out_.end(), olap_.begin(), olap_.begin() + kHop * 2);
    std::copy(olap_.begin() + kHop * 2, olap_.end(), olap_.begin());
    std::fill(olap_.end() - kHop * 2, olap_.end(), 0.0f);
    prev_ = start;
    aPos_ += kHop * rate_;
    return true;
}

}  // namespace audio
