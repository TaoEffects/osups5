// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/image.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "core/fsutil.h"
#include "core/log.h"
#include "core/thread.h"
#include "stb/stb_image.h"

namespace gfx {
namespace {

void halve(ImageData &img) {
    const int w = img.width / 2, h = img.height / 2;
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) {
                const uint8_t *p = &img.rgba[(static_cast<size_t>(y * 2) * img.width + x * 2) * 4 + c];
                const size_t stride = static_cast<size_t>(img.width) * 4;
                out[(static_cast<size_t>(y) * w + x) * 4 + c] =
                    static_cast<uint8_t>((p[0] + p[4] + p[stride] + p[stride + 4] + 2) / 4);
            }
    img.width = w;
    img.height = h;
    img.rgba.swap(out);
}

bool finish(unsigned char *pixels, int w, int h, int maxDim, ImageData &out) {
    if (!pixels) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    while (maxDim > 0 && (out.width > maxDim || out.height > maxDim) && out.width > 1 && out.height > 1) halve(out);
    return true;
}

}  // namespace

void boxBlur(ImageData &img, int radius) {
    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0 || radius <= 0) return;
    std::vector<uint8_t> tmp(img.rgba.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int dir = 0; dir < 2; ++dir) {
            const int len = dir == 0 ? w : h, lines = dir == 0 ? h : w;
            for (int l = 0; l < lines; ++l) {
                for (int c = 0; c < 4; ++c) {
                    auto at = [&](int i) -> uint8_t & {
                        i = i < 0 ? 0 : (i >= len ? len - 1 : i);
                        const int x = dir == 0 ? i : l, y = dir == 0 ? l : i;
                        return img.rgba[(static_cast<size_t>(y) * w + x) * 4 + c];
                    };
                    int sum = 0;
                    for (int i = -radius; i <= radius; ++i) sum += at(i);
                    for (int i = 0; i < len; ++i) {
                        const int x = dir == 0 ? i : l, y = dir == 0 ? l : i;
                        tmp[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(sum / (2 * radius + 1));
                        sum += at(i + radius + 1) - at(i - radius);
                    }
                }
            }
            img.rgba.swap(tmp);
        }
    }
}

bool decodeImageMemory(const uint8_t *data, size_t size, int maxDim, ImageData &out) {
    int w = 0, h = 0, n = 0;
    unsigned char *pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    return finish(pixels, w, h, maxDim, out);
}

bool decodeImageFile(const std::string &path, int maxDim, ImageData &out) {
    std::vector<uint8_t> bytes;
    if (!fsu::readFile(path, bytes, 64u << 20)) return false;
    return decodeImageMemory(bytes.data(), bytes.size(), maxDim, out);
}

void ImageLoader::start(int threads, const std::string &cacheDir) {
    if (running_) return;
    running_ = true;
    cacheDir_ = cacheDir;
    if (!cacheDir_.empty()) fsu::makeDirs(cacheDir_);
    threads_.resize(static_cast<size_t>(threads));
    for (pthread_t &t : threads_) thr::start(t, threadMain, this, "images", 1024 * 1024);
}

void ImageLoader::stop() {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    for (pthread_t &t : threads_) pthread_join(t, nullptr);
    threads_.clear();
}

uint64_t ImageLoader::request(const std::string &path, int maxDim, int blurRadius) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t ticket = next_++;
    jobs_.push_back(Job{ticket, path, maxDim, blurRadius});
    return ticket;
}

std::vector<ImageLoader::Done> ImageLoader::poll(int maxUploads) {
    std::vector<Done> done;
    for (int i = 0; i < maxUploads; ++i) {
        Result r;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (results_.empty()) break;
            r = std::move(results_.front());
            results_.pop_front();
        }
        Done d{r.ticket, r.ok, Texture{}};
        if (r.ok) d.texture = createTexture(r.image.width, r.image.height, r.image.rgba.data(), true);
        done.push_back(d);
    }
    return done;
}

namespace {

// Cache disque : en-tête "RIMG", largeur, hauteur, puis les pixels RGBA bruts.
std::string cachePath(const std::string &dir, const std::string &path, int maxDim, int blur) {
    struct stat st {};
    stat(path.c_str(), &st);
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&h](const void *p, size_t n) {
        const auto *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    };
    mixIn(path.data(), path.size());
    mixIn(&maxDim, sizeof(maxDim));
    mixIn(&blur, sizeof(blur));
    const long long size = static_cast<long long>(st.st_size), mtime = static_cast<long long>(st.st_mtime);
    mixIn(&size, sizeof(size));
    mixIn(&mtime, sizeof(mtime));
    char name[40];
    snprintf(name, sizeof(name), "/%016llx.rimg", static_cast<unsigned long long>(h));
    return dir + name;
}

bool readCache(const std::string &file, ImageData &out) {
    std::vector<uint8_t> data;
    if (!fsu::readFile(file, data, 32u << 20) || data.size() < 12 || memcmp(data.data(), "RIMG", 4) != 0) return false;
    int32_t w = 0, h = 0;
    memcpy(&w, data.data() + 4, 4);
    memcpy(&h, data.data() + 8, 4);
    if (w <= 0 || h <= 0 || data.size() != 12 + static_cast<size_t>(w) * h * 4) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(data.begin() + 12, data.end());
    return true;
}

void writeCache(const std::string &file, const ImageData &img) {
    std::vector<uint8_t> data(12 + img.rgba.size());
    memcpy(data.data(), "RIMG", 4);
    const int32_t w = img.width, h = img.height;
    memcpy(data.data() + 4, &w, 4);
    memcpy(data.data() + 8, &h, 4);
    memcpy(data.data() + 12, img.rgba.data(), img.rgba.size());
    fsu::writeFileAtomic(file, data.data(), data.size());
}

}  // namespace

void *ImageLoader::threadMain(void *self) {
    auto *loader = static_cast<ImageLoader *>(self);
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(loader->mutex_);
            if (!loader->running_) return nullptr;
            if (!loader->jobs_.empty()) {
                // Les demandes les plus récentes d'abord (ce qui est à l'écran) ; les
                // trop anciennes sont abandonnées (défilement rapide).
                while (loader->jobs_.size() > 24) {
                    loader->results_.push_back(Result{loader->jobs_.front().ticket, false, {}});
                    loader->jobs_.pop_front();
                }
                job = loader->jobs_.back();
                loader->jobs_.pop_back();
            } else {
                job.ticket = 0;
            }
        }
        if (job.ticket == 0) {
            usleep(5000);
            continue;
        }
        Result r{job.ticket, false, {}};
        const std::string cache = loader->cacheDir_.empty() ? "" : cachePath(loader->cacheDir_, job.path, job.maxDim, job.blur);
        if (!cache.empty() && readCache(cache, r.image)) {
            r.ok = true;
        } else {
            r.ok = decodeImageFile(job.path, job.maxDim, r.image);
            if (r.ok && job.blur > 0) boxBlur(r.image, job.blur);
            if (r.ok && !cache.empty()) writeCache(cache, r.image);
            if (!r.ok) LOG("image: echec de decodage %s", job.path.c_str());
        }
        std::lock_guard<std::mutex> lock(loader->mutex_);
        loader->results_.push_back(std::move(r));
    }
}

}  // namespace gfx
