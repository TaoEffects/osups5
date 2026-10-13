// SPDX-License-Identifier: GPL-3.0-or-later
// Images (JPEG/PNG/BMP via stb_image). Le décodage, coûteux, se fait sur un thread
// de travail ; seul l'envoi au GPU se fait sur le thread principal.
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <pthread.h>

#include "render/batch.h"

namespace gfx {

struct ImageData {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Décode un fichier ; réduit l'image si un côté dépasse maxDim (divisions par 2).
bool decodeImageFile(const std::string &path, int maxDim, ImageData &out);
// Idem depuis la mémoire (images téléchargées).
bool decodeImageMemory(const uint8_t *data, size_t size, int maxDim, ImageData &out);
// Flou par boîtes (3 passes ≈ gaussien), pour les petites images.
void boxBlur(ImageData &img, int radius);

// Chargeur asynchrone : request() renvoie un ticket, poll() rend les images prêtes.
class ImageLoader {
public:
    struct Done {
        uint64_t ticket;
        bool ok;
        Texture texture;
    };
    // Démarre `threads` décodeurs ; cacheDir (optionnel) garde les images réduites sur disque.
    void start(int threads = 3, const std::string &cacheDir = "");
    void stop();
    // blurRadius > 0 : flou (fonds de menus), appliqué après réduction.
    uint64_t request(const std::string &path, int maxDim, int blurRadius = 0);
    // Envoie au GPU au plus `maxUploads` images décodées (lissage de la charge).
    std::vector<Done> poll(int maxUploads = 1);

private:
    static void *threadMain(void *self);
    struct Job {
        uint64_t ticket;
        std::string path;
        int maxDim;
        int blur;
    };
    struct Result {
        uint64_t ticket;
        bool ok;
        ImageData image;
    };
    std::mutex mutex_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    std::vector<pthread_t> threads_;
    std::string cacheDir_;
    bool running_ = false;
    uint64_t next_ = 1;
};

}  // namespace gfx
