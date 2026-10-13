// SPDX-License-Identifier: GPL-3.0-or-later
// Bibliothèque des maps installées. Chaque sous-dossier du dossier "songs" est un
// set (une .osz décompressée). Un index en cache évite de relire tous les .osu au
// lancement ; la liste des dossiers est aussi tenue dans folders.txt, au cas où la
// console refuserait de lister les dossiers.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace game {

struct DiffInfo {
    std::string file;
    std::string version;
    int beatmapId = 0;
    float stars = -1;  // -1 = inconnu (map importée à la main)
    float ar = 5, od = 5, cs = 5, hp = 5;
    double bpm = 0, lengthMs = 0;
    double beatOffset = 0;  // instant du premier point de timing rouge (ms)
    int objects = 0, circles = 0, sliders = 0, spinners = 0;
    int previewTime = -1;
    std::string audioFile, background;
};

struct SetInfo {
    std::string folder;  // nom du sous-dossier
    int setId = -1;
    std::string title, titleUnicode, artist, artistUnicode, creator, source, tags;
    std::vector<DiffInfo> diffs;  // triées par difficulté croissante
    std::string path(const std::string &root) const { return root + "/" + folder; }
    // Titre et artiste à afficher : langue d'origine si demandé et disponible.
    const std::string &shownTitle(bool original) const { return original && !titleUnicode.empty() ? titleUnicode : title; }
    const std::string &shownArtist(bool original) const { return original && !artistUnicode.empty() ? artistUnicode : artist; }
};

class Library {
public:
    void setRoot(const std::string &songsDir);
    const std::string &root() const { return root_; }

    // Lance un (re)balayage ; scanStep() traite quelques dossiers par appel pour
    // ne pas figer l'affichage. Renvoie true quand c'est fini.
    void beginScan();
    bool scanStep(int maxFolders);
    bool scanning() const { return scanning_; }
    float scanProgress() const;

    // Ajoute (ou relit) un dossier, par exemple après un téléchargement.
    bool addFolder(const std::string &folder);
    // Étoiles connues (fournies par le miroir au téléchargement).
    void setStars(const std::string &folder, const std::unordered_map<int, float> &starsById);

    const std::vector<SetInfo> &sets() const { return sets_; }
    int findSet(const std::string &folder) const;
    bool hasSetId(int setId) const;

private:
    bool readFolder(const std::string &folder, SetInfo &out);
    void loadCache();
    void saveCache() const;
    void saveFolderList() const;
    void sortSets();

    std::string root_;
    std::vector<SetInfo> sets_;
    std::unordered_map<std::string, SetInfo> cache_;
    std::vector<std::string> pending_;
    size_t pendingTotal_ = 0;
    bool scanning_ = false;
    bool dirty_ = false;
};

}  // namespace game
