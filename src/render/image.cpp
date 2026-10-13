// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/image.h"

#include <unistd.h>

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

void ImageLoader::start() {
    if (running_) return;
    running_ = true;
    thr::start(thread_, threadMain, this, "images", 1024 * 1024);
}

void ImageLoader::stop() {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    pthread_join(thread_, nullptr);
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

void *ImageLoader::threadMain(void *self) {
    auto *loader = static_cast<ImageLoader *>(self);
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(loader->mutex_);
            if (!loader->running_) return nullptr;
            if (!loader->jobs_.empty()) {
                // On ne garde que les demandes récentes : en défilant vite, les
                // anciennes ne servent plus.
                while (loader->jobs_.size() > 6) {
                    loader->results_.push_back(Result{loader->jobs_.front().ticket, false, {}});
                    loader->jobs_.pop_front();
                }
                job = loader->jobs_.front();
                loader->jobs_.pop_front();
            } else {
                job.ticket = 0;
            }
        }
        if (job.ticket == 0) {
            usleep(10000);
            continue;
        }
        Result r{job.ticket, false, {}};
        r.ok = decodeImageFile(job.path, job.maxDim, r.image);
        if (r.ok && job.blur > 0) boxBlur(r.image, job.blur);
        if (!r.ok) LOG("image: echec de decodage %s", job.path.c_str());
        std::lock_guard<std::mutex> lock(loader->mutex_);
        loader->results_.push_back(std::move(r));
    }
}

}  // namespace gfx
