// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music.h"

#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include "audio/mixer.h"
#include "core/fsutil.h"
#include "core/log.h"
#include "core/thread.h"
#include "dr_libs/dr_mp3.h"
#include "dr_libs/dr_wav.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

namespace audio {
namespace {

constexpr int kRate = Mixer::kRate;
constexpr size_t kRingFrames = kRate * 2;  // 2 s d'avance au maximum

void toStereo(const float *in, int channels, int frames, float *out) {
    for (int i = 0; i < frames; ++i) {
        if (channels == 1) {
            out[i * 2] = out[i * 2 + 1] = in[i];
        } else {
            out[i * 2] = in[i * channels];
            out[i * 2 + 1] = in[i * channels + 1];
        }
    }
}

class Mp3Decoder : public Decoder {
public:
    explicit Mp3Decoder(std::shared_ptr<std::vector<uint8_t>> d) : data_(std::move(d)) {}
    ~Mp3Decoder() override {
        if (ok_) drmp3_uninit(&mp3_);
    }
    bool init() {
        ok_ = drmp3_init_memory(&mp3_, data_->data(), data_->size(), nullptr);
        if (ok_) rate = static_cast<int>(mp3_.sampleRate);
        return ok_ && mp3_.channels >= 1;
    }
    int read(float *stereo, int frames) override {
        tmp_.resize(static_cast<size_t>(frames) * mp3_.channels);
        const int n = static_cast<int>(drmp3_read_pcm_frames_f32(&mp3_, static_cast<drmp3_uint64>(frames), tmp_.data()));
        toStereo(tmp_.data(), static_cast<int>(mp3_.channels), n, stereo);
        return n;
    }
    bool seek(uint64_t frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame); }

private:
    std::shared_ptr<std::vector<uint8_t>> data_;
    drmp3 mp3_{};
    bool ok_ = false;
    std::vector<float> tmp_;
};

class VorbisDecoder : public Decoder {
public:
    explicit VorbisDecoder(std::shared_ptr<std::vector<uint8_t>> d) : data_(std::move(d)) {}
    ~VorbisDecoder() override {
        if (v_) stb_vorbis_close(v_);
    }
    bool init() {
        int err = 0;
        v_ = stb_vorbis_open_memory(data_->data(), static_cast<int>(data_->size()), &err, nullptr);
        if (!v_) return false;
        const stb_vorbis_info info = stb_vorbis_get_info(v_);
        rate = static_cast<int>(info.sample_rate);
        lengthFrames = stb_vorbis_stream_length_in_samples(v_);
        return true;
    }
    int read(float *stereo, int frames) override {
        return stb_vorbis_get_samples_float_interleaved(v_, 2, stereo, frames * 2);
    }
    bool seek(uint64_t frame) override { return stb_vorbis_seek(v_, static_cast<unsigned>(frame)) != 0; }

private:
    std::shared_ptr<std::vector<uint8_t>> data_;
    stb_vorbis *v_ = nullptr;
};

class WavDecoder : public Decoder {
public:
    explicit WavDecoder(std::shared_ptr<std::vector<uint8_t>> d) : data_(std::move(d)) {}
    ~WavDecoder() override {
        if (ok_) drwav_uninit(&wav_);
    }
    bool init() {
        ok_ = drwav_init_memory(&wav_, data_->data(), data_->size(), nullptr);
        if (!ok_ || wav_.channels < 1) return false;
        rate = static_cast<int>(wav_.sampleRate);
        lengthFrames = wav_.totalPCMFrameCount;
        return true;
    }
    int read(float *stereo, int frames) override {
        tmp_.resize(static_cast<size_t>(frames) * wav_.channels);
        const int n = static_cast<int>(drwav_read_pcm_frames_f32(&wav_, static_cast<drwav_uint64>(frames), tmp_.data()));
        toStereo(tmp_.data(), static_cast<int>(wav_.channels), n, stereo);
        return n;
    }
    bool seek(uint64_t frame) override { return drwav_seek_to_pcm_frame(&wav_, frame); }

private:
    std::shared_ptr<std::vector<uint8_t>> data_;
    drwav wav_{};
    bool ok_ = false;
    std::vector<float> tmp_;
};

template <typename T>
std::unique_ptr<Decoder> tryOpen(const std::shared_ptr<std::vector<uint8_t>> &data) {
    auto d = std::make_unique<T>(data);
    if (!d->init()) return nullptr;
    return d;
}

}  // namespace

std::unique_ptr<Decoder> openDecoder(std::shared_ptr<std::vector<uint8_t>> data, const std::string &nameHint) {
    if (!data || data->size() < 16) return nullptr;
    const uint8_t *p = data->data();
    // On se fie au contenu plutôt qu'à l'extension (beaucoup de .mp3 sont des ogg...).
    if (!memcmp(p, "OggS", 4)) return tryOpen<VorbisDecoder>(data);
    if (!memcmp(p, "RIFF", 4)) return tryOpen<WavDecoder>(data);
    if (auto d = tryOpen<Mp3Decoder>(data)) return d;
    LOG("music: format non reconnu pour %s", nameHint.c_str());
    return nullptr;
}

bool decodeWholeFile(const std::string &path, std::vector<float> &out) {
    auto data = std::make_shared<std::vector<uint8_t>>();
    if (!fsu::readFile(path, *data, 16u << 20)) return false;
    auto dec = openDecoder(data, path);
    if (!dec) return false;
    std::vector<float> src;
    float buf[4096 * 2];
    for (;;) {
        const int n = dec->read(buf, 4096);
        if (n <= 0) break;
        src.insert(src.end(), buf, buf + n * 2);
        if (src.size() > static_cast<size_t>(kRate) * 2 * 20) break;  // 20 s max
    }
    const size_t srcFrames = src.size() / 2;
    out.clear();
    if (srcFrames == 0) return false;
    const double step = static_cast<double>(dec->rate) / kRate;
    const size_t outFrames = static_cast<size_t>(static_cast<double>(srcFrames) / step);
    out.resize(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i) {
        const double pos = static_cast<double>(i) * step;
        const size_t a = static_cast<size_t>(pos);
        const size_t b = a + 1 < srcFrames ? a + 1 : a;
        const float f = static_cast<float>(pos - static_cast<double>(a));
        out[i * 2] = src[a * 2] + (src[b * 2] - src[a * 2]) * f;
        out[i * 2 + 1] = src[a * 2 + 1] + (src[b * 2 + 1] - src[a * 2 + 1]) * f;
    }
    return true;
}

Music::~Music() { unload(); }

bool Music::load(const std::string &path) {
    unload();
    auto data = std::make_shared<std::vector<uint8_t>>();
    if (!fsu::readFile(path, *data, 128u << 20)) {
        LOG("music: lecture impossible: %s", path.c_str());
        return false;
    }
    auto decoder = openDecoder(data, path);
    if (!decoder) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    decoder_ = std::move(decoder);
    data_ = data;
    path_ = path;
    ring_.assign(kRingFrames * 2, 0.0f);
    ringRead_ = ringCount_ = 0;
    ringStartPos_ = 0;
    pendingSeek_ = 0;
    decoderEof_ = false;
    playPos_.store(0);
    ended_.store(false);
    playing_.store(false);
    snapValid_.store(false);
    threadRun_.store(true);
    thr::start(thread_, decodeThread, this, "musique", 512 * 1024);
    LOG("music: %s charge (%d Hz, %.1f Mo)", path.c_str(), decoder_->rate, data->size() / 1048576.0);
    return true;
}

void Music::unload() {
    if (!decoder_) return;
    playing_.store(false);
    threadRun_.store(false);
    pthread_join(thread_, nullptr);
    std::lock_guard<std::mutex> lock(mutex_);
    decoder_.reset();
    data_.reset();
}

double Music::lengthSeconds() const {
    if (!decoder_ || decoder_->lengthFrames == 0) return 0;
    return static_cast<double>(decoder_->lengthFrames) / decoder_->rate;
}

void Music::play(double seconds) {
    if (!decoder_) return;
    const int64_t target = static_cast<int64_t>(std::llround(seconds * kRate));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingSeek_ = target;
        ringCount_ = 0;
        ringRead_ = 0;
        ringStartPos_ = target < 0 ? 0 : target;
    }
    playPos_.store(static_cast<double>(target));
    snapValid_.store(false);
    ended_.store(false);
    playing_.store(true);
}

void Music::setRate(double rate, bool preservePitch) {
    rate = std::clamp(rate, 0.25, 3.0);
    if (rate == rate_.load() && preservePitch == preservePitch_.load()) return;
    rate_.store(rate);
    preservePitch_.store(preservePitch);
    if (!decoder_) return;
    // On repart de la position courante : le tampon décodé l'a été à l'ancienne vitesse.
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t at = static_cast<int64_t>(std::max(0.0, playPos_.load()));
    pendingSeek_ = at;
    ringCount_ = 0;
    ringRead_ = 0;
    ringStartPos_ = at;
    snapValid_.store(false);
}

void Music::pause() {
    playing_.store(false);
    snapValid_.store(false);
}

void Music::resume() {
    if (decoder_ && !ended_.load()) playing_.store(true);
}

double Music::time(const Mixer &mixer) const {
    if (!playing_.load() || !snapValid_.load()) return playPos_.load() / kRate;
    const double sinceBlock = mixer.clockSeconds() - static_cast<double>(snapMixerFrame_.load()) / kRate;
    return snapPos_.load() / kRate + (sinceBlock > 0 ? sinceBlock * snapRate_.load() : 0);
}

void Music::render(float *out, int frames, uint64_t mixerFrame) {
    if (!playing_.load(std::memory_order_relaxed)) return;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;  // le décodeur travaille : un bloc de silence, sans avancer
    double pos = playPos_.load(std::memory_order_relaxed);
    // Avant que le décodeur ait traité un changement de vitesse, le silence de
    // départ avance déjà à la nouvelle vitesse.
    const double rate = pos < 0 ? rate_.load(std::memory_order_relaxed) : ringRate_;
    snapPos_.store(pos, std::memory_order_relaxed);
    snapRate_.store(rate, std::memory_order_relaxed);
    snapMixerFrame_.store(mixerFrame, std::memory_order_relaxed);
    snapValid_.store(true, std::memory_order_release);
    const float vol = volume_.load(std::memory_order_relaxed);
    float energy = 0;
    for (int i = 0; i < frames; ++i) {
        if (pos < 0) {
            pos += rate;
            if (pos >= 0) pos = 0;
            continue;
        }
        if (pendingSeek_ != INT64_MIN || ringCount_ == 0) {
            if (decoderEof_ && pendingSeek_ == INT64_MIN) {
                if (loop_.load()) {
                    const int64_t target = static_cast<int64_t>(loopStart_.load() * kRate);
                    pendingSeek_ = target;
                    ringStartPos_ = target < 0 ? 0 : target;
                    pos = target;
                    snapValid_.store(false);
                } else {
                    ended_.store(true);
                    playing_.store(false);
                }
            }
            break;  // tampon vide : on attend le décodeur sans avancer l'horloge
        }
        const float l = ring_[ringRead_ * 2], r = ring_[ringRead_ * 2 + 1];
        out[i * 2] += l * vol;
        out[i * 2 + 1] += r * vol;
        energy += l * l + r * r;
        history_[historyPos_] = (l + r) * 0.5f;
        historyPos_ = (historyPos_ + 1) % history_.size();
        ringRead_ = (ringRead_ + 1) % kRingFrames;
        --ringCount_;
        pos += ringRate_;
    }
    playPos_.store(pos, std::memory_order_relaxed);
    const float rms = std::sqrt(energy / static_cast<float>(frames * 2));
    level_.store(level_.load() * 0.7f + rms * 0.3f, std::memory_order_relaxed);
    analyse();
}

std::array<float, Music::kBands> Music::spectrum() const {
    std::array<float, kBands> out{};
    for (int i = 0; i < kBands; ++i) out[static_cast<size_t>(i)] = bands_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    return out;
}

void Music::analyse() {
    // Goertzel sur les 1024 derniers échantillons, fréquences réparties en log.
    // ~64 x 1024 multiplications par bloc audio : négligeable.
    const size_t n = history_.size();
    for (int b = 0; b < kBands; ++b) {
        const float freq = 40.0f * std::pow(300.0f, static_cast<float>(b) / (kBands - 1));
        const float w = 2.0f * 3.14159265f * freq / kRate;
        const float coeff = 2.0f * std::cos(w);
        float s1 = 0, s2 = 0;
        for (size_t i = 0; i < n; ++i) {
            const size_t idx = (historyPos_ + i) % n;
            // Fenêtre de Hann pour limiter les fuites.
            const float win = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(n - 1));
            const float s0 = history_[idx] * win + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        float mag = std::sqrt(std::max(0.0f, power)) / static_cast<float>(n) * 4.0f;
        mag *= 1.0f + static_cast<float>(b) * 0.06f;  // compense la pente naturelle des aigus
        const float v = std::min(1.0f, std::sqrt(mag));
        std::atomic<float> &slot = bands_[static_cast<size_t>(b)];
        const float old = slot.load(std::memory_order_relaxed);
        slot.store(v > old ? v : old * 0.88f, std::memory_order_relaxed);
    }
}

void *Music::decodeThread(void *self) {
    static_cast<Music *>(self)->decodeLoop();
    return nullptr;
}

void Music::decodeLoop() {
    std::vector<float> src(4097 * 2);  // trame précédente + bloc lu
    int srcCount = 0, srcIdx = 0;
    double frac = 0;
    constexpr int kChunk = 1024;
    std::vector<float> chunk(kChunk * 2);
    std::vector<float> stretched(kChunk * 4 * 2);
    Stretcher stretcher;
    double rate = 1.0;
    bool stretch = false, finished = false;
    while (threadRun_.load()) {
        bool seekNow = false;
        size_t space = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingSeek_ != INT64_MIN) {
                const int64_t seekTarget = pendingSeek_ < 0 ? 0 : pendingSeek_;
                const uint64_t srcFrame =
                    static_cast<uint64_t>(static_cast<double>(seekTarget) * decoder_->rate / kRate);
                decoder_->seek(srcFrame);
                ringCount_ = 0;
                ringRead_ = 0;
                ringStartPos_ = seekTarget;
                decoderEof_ = false;
                pendingSeek_ = INT64_MIN;
                rate = rate_.load();
                stretch = preservePitch_.load() && std::fabs(rate - 1.0) > 1e-6;
                ringRate_ = rate;
                seekNow = true;
            }
            space = kRingFrames - ringCount_;
        }
        if (seekNow) {
            srcCount = srcIdx = 0;
            frac = 0;
            finished = false;
            stretcher.reset(rate);
        }
        if (space < kChunk * 3 || decoderEof_) {
            usleep(3000);
            continue;
        }
        // Rééchantillonnage vers 48 kHz (interpolation linéaire). Sans tempo
        // séparé (Nightcore), la vitesse s'applique directement ici.
        const double step = static_cast<double>(decoder_->rate) / kRate * (stretch ? 1.0 : rate);
        int produced = 0;
        bool eof = false;
        while (produced < kChunk && !finished) {
            if (srcIdx + 1 >= srcCount) {
                // Garde la dernière trame comme point de départ de l'interpolation.
                if (srcCount > 0) {
                    src[0] = src[(srcCount - 1) * 2];
                    src[1] = src[(srcCount - 1) * 2 + 1];
                    srcIdx -= srcCount - 1;
                    srcCount = 1;
                }
                const int n = decoder_->read(&src[static_cast<size_t>(srcCount) * 2], 4096);
                if (n <= 0) {
                    eof = true;
                    break;
                }
                srcCount += n;
                continue;
            }
            const float f = static_cast<float>(frac);
            const float *a = &src[static_cast<size_t>(srcIdx) * 2];
            chunk[produced * 2] = a[0] + (a[2] - a[0]) * f;
            chunk[produced * 2 + 1] = a[1] + (a[3] - a[1]) * f;
            ++produced;
            frac += step;
            const int whole = static_cast<int>(frac);
            srcIdx += whole;
            frac -= whole;
        }
        const float *out = chunk.data();
        int outFrames = produced;
        if (stretch) {
            stretcher.push(chunk.data(), produced);
            if (eof && !finished) {
                stretcher.finish();
                finished = true;
            }
            outFrames = stretcher.pull(stretched.data(), static_cast<int>(std::min(stretched.size() / 2, space)));
            out = stretched.data();
            // Il reste parfois de la sortie à vider après la fin du décodeur.
            if (eof || finished) eof = stretcher.available() == 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingSeek_ != INT64_MIN) continue;  // un saut est arrivé entre-temps : bloc périmé
        for (int i = 0; i < outFrames; ++i) {
            const size_t w = (ringRead_ + ringCount_) % kRingFrames;
            ring_[w * 2] = out[i * 2];
            ring_[w * 2 + 1] = out[i * 2 + 1];
            ++ringCount_;
        }
        if (eof) decoderEof_ = true;
    }
}

}  // namespace audio
