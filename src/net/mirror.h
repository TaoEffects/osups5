// SPDX-License-Identifier: GPL-3.0-or-later
// Accès aux miroirs de beatmaps (osu.direct par défaut) : recherche, vignettes,
// téléchargement et décompression des .osz. Tout se passe sur un thread de travail ;
// l'écran interroge l'état à chaque image. Aucune connexion aux serveurs officiels.
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <pthread.h>

#include "render/image.h"

namespace net {

struct RemoteDiff {
    int id = 0;
    std::string version;
    float stars = 0;
    int mode = 0;
};

struct RemoteSet {
    int id = 0;
    std::string title, artist, creator, status;
    double bpm = 0;
    int lengthSec = 0;
    long long playCount = 0, favourites = 0;
    std::string coverUrl;  // bandeau "card@2x" (800x280)
    std::vector<RemoteDiff> diffs;
};

enum class Sort { Popular, Recent, Favourites };

struct DownloadState {
    int setId = 0;
    std::string label;
    std::atomic<uint64_t> received{0}, total{0};
    std::atomic<int> phase{0};  // 0 attente, 1 téléchargement, 2 extraction, 3 fini, 4 erreur
    std::string error;
    std::string folder;         // dossier créé (phase 3)
    bool quiet = false;         // pas de notification (maps de base du premier lancement)
};

class Mirror {
public:
    void start(const std::string &baseUrl, const std::string &songsDir);
    void stop();

    // Recherche (remplace la précédente). Résultat via searchResults().
    void search(const std::string &query, Sort sort, int page);
    bool searchBusy() const { return searchBusy_.load(); }
    // Renvoie true (une fois) quand de nouveaux résultats sont arrivés.
    bool takeResults(std::vector<RemoteSet> &out, std::string &error);

    // Vignettes : image décodée prête à envoyer au GPU.
    void requestCover(int setId, const std::string &url);
    bool takeCover(int &setId, gfx::ImageData &image);

    // quiet : pas de notification à la fin. Si seul set.id est rempli, les infos
    // (titre, étoiles) sont demandées au miroir avant le téléchargement.
    std::shared_ptr<DownloadState> download(const RemoteSet &set, bool quiet = false);
    std::vector<std::shared_ptr<DownloadState>> downloads();

private:
    static void *threadMain(void *self);
    static void *coverMain(void *self);
    void loop();
    void coverLoop();
    void doSearch();
    void doDownload(const std::shared_ptr<DownloadState> &d, const RemoteSet &set);

    std::string base_, songsDir_;
    std::mutex mutex_;
    pthread_t thread_{};
    std::vector<pthread_t> coverThreads_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelAll_{false};  // vrai à l'arrêt de l'appli

    bool searchPending_ = false;
    std::string query_;
    Sort sort_ = Sort::Popular;
    int page_ = 0;
    std::atomic<bool> searchBusy_{false};
    bool resultsReady_ = false;
    std::vector<RemoteSet> results_;
    std::string resultsError_;

    std::deque<std::pair<int, std::string>> coverJobs_;
    std::deque<std::pair<int, gfx::ImageData>> covers_;

    std::deque<std::pair<std::shared_ptr<DownloadState>, RemoteSet>> downloadJobs_;
    std::vector<std::shared_ptr<DownloadState>> downloads_;
};

// Décompresse une .osz dans `destDir` (les vidéos sont ignorées pour gagner de la place).
bool extractOsz(const std::string &oszPath, const std::string &destDir, std::string &error);

}  // namespace net
