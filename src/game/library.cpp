// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/library.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/fsutil.h"
#include "core/log.h"
#include "game/beatmap.h"

namespace game {
namespace {

constexpr const char *kCacheName = "library.cache";
constexpr const char *kFolderList = "folders.txt";
constexpr const char *kStarsFile = ".stars";
constexpr int kCacheVersion = 3;

std::vector<std::string> splitTabs(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i)
        if (i == line.size() || line[i] == '\t') {
            out.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

std::string clean(std::string s) {
    for (char &c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::unordered_map<int, float> readStars(const std::string &folderPath) {
    std::unordered_map<int, float> stars;
    std::string text;
    if (!fsu::readText(folderPath + "/" + kStarsFile, text, 1 << 20)) return stars;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        int id = 0;
        float s = 0;
        if (sscanf(line.c_str(), "%d %f", &id, &s) == 2) stars[id] = s;
    }
    return stars;
}

}  // namespace

void Library::setRoot(const std::string &songsDir) {
    root_ = songsDir;
    fsu::makeDirs(root_);
    loadCache();
}

int Library::findSet(const std::string &folder) const {
    for (size_t i = 0; i < sets_.size(); ++i)
        if (sets_[i].folder == folder) return static_cast<int>(i);
    return -1;
}

bool Library::hasSetId(int setId) const {
    if (setId <= 0) return false;
    for (const SetInfo &s : sets_)
        if (s.setId == setId) return true;
    return false;
}

float Library::scanProgress() const {
    if (pendingTotal_ == 0) return 1;
    return 1.0f - static_cast<float>(pending_.size()) / static_cast<float>(pendingTotal_);
}

void Library::beginScan() {
    pending_.clear();
    std::vector<fsu::Entry> entries;
    if (fsu::listDir(root_, entries)) {
        for (const fsu::Entry &e : entries)
            if (e.isDir) pending_.push_back(e.name);
    } else {
        // Pas de listage possible : on se fie à la liste tenue par l'appli.
        std::string text;
        if (fsu::readText(root_ + "/" + kFolderList, text)) {
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                std::string name = text.substr(pos, end - pos);
                pos = end + 1;
                if (!name.empty() && name.back() == '\r') name.pop_back();
                if (!name.empty()) pending_.push_back(name);
            }
        }
        LOG("library: listage impossible, %zu dossiers depuis %s", pending_.size(), kFolderList);
    }
    std::sort(pending_.begin(), pending_.end());
    pendingTotal_ = pending_.size();
    sets_.clear();
    scanning_ = true;
    LOG("library: %zu dossiers a examiner dans %s", pendingTotal_, root_.c_str());
}

bool Library::scanStep(int maxFolders) {
    if (!scanning_) return true;
    for (int n = 0; n < maxFolders && !pending_.empty(); ++n) {
        const std::string folder = pending_.back();
        pending_.pop_back();
        auto it = cache_.find(folder);
        if (it != cache_.end()) {
            sets_.push_back(it->second);
            continue;
        }
        SetInfo set;
        if (readFolder(folder, set)) {
            cache_[folder] = set;
            sets_.push_back(set);
            dirty_ = true;
        }
    }
    if (!pending_.empty()) return false;
    scanning_ = false;
    sortSets();
    if (dirty_) saveCache();
    saveFolderList();
    LOG("library: %zu sets prets", sets_.size());
    return true;
}

bool Library::addFolder(const std::string &folder) {
    SetInfo set;
    if (!readFolder(folder, set)) return false;
    cache_[folder] = set;
    const int existing = findSet(folder);
    if (existing >= 0)
        sets_[static_cast<size_t>(existing)] = set;
    else
        sets_.push_back(set);
    sortSets();
    saveCache();
    saveFolderList();
    return true;
}

void Library::setStars(const std::string &folder, const std::unordered_map<int, float> &starsById) {
    std::string text;
    char line[64];
    for (const auto &kv : starsById) {
        snprintf(line, sizeof(line), "%d %.3f\n", kv.first, kv.second);
        text += line;
    }
    fsu::writeFileAtomic(root_ + "/" + folder + "/" + kStarsFile, text.data(), text.size());
}

bool Library::readFolder(const std::string &folder, SetInfo &out) {
    const std::string path = root_ + "/" + folder;
    std::vector<fsu::Entry> entries;
    if (!fsu::listDir(path, entries)) {
        // Repli : la liste écrite à l'installation.
        std::string text;
        if (!fsu::readText(path + "/.files", text)) return false;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string name = text.substr(pos, end - pos);
            pos = end + 1;
            if (!name.empty() && name.find('/') == std::string::npos) entries.push_back({name, false});
        }
    }
    const std::unordered_map<int, float> stars = readStars(path);
    out = SetInfo{};
    out.folder = folder;
    for (const fsu::Entry &e : entries) {
        if (e.isDir || fsu::extensionLower(e.name) != "osu") continue;
        std::string text;
        if (!fsu::readText(path + "/" + e.name, text, 8u << 20)) continue;
        Beatmap m;
        if (!parseBeatmap(text, m, false) || m.mode != 0 || m.objects.empty()) continue;
        DiffInfo d;
        d.file = e.name;
        d.version = m.version;
        d.beatmapId = m.beatmapId;
        auto st = stars.find(m.beatmapId);
        if (st != stars.end()) d.stars = st->second;
        d.ar = m.ar;
        d.od = m.od;
        d.cs = m.cs;
        d.hp = m.hp;
        d.bpm = m.bpmMain;
        d.lengthMs = m.objects.back().time - m.objects.front().time;
        d.objects = static_cast<int>(m.objects.size());
        d.circles = m.circles;
        d.sliders = m.sliders;
        d.spinners = m.spinners;
        d.previewTime = m.previewTime;
        for (const TimingPoint &tp : m.timing)
            if (tp.uninherited) {
                d.beatOffset = tp.time;
                break;
            }
        d.audioFile = m.audioFile;
        d.background = m.background;
        if (out.diffs.empty()) {
            out.setId = m.setId;
            out.title = m.title;
            out.titleUnicode = m.titleUnicode;
            out.artist = m.artist;
            out.artistUnicode = m.artistUnicode;
            out.creator = m.creator;
            out.source = m.source;
            out.tags = m.tags;
        }
        out.diffs.push_back(d);
    }
    if (out.diffs.empty()) return false;
    // Tri par difficulté : étoiles si connues, sinon densité de notes.
    std::sort(out.diffs.begin(), out.diffs.end(), [](const DiffInfo &a, const DiffInfo &b) {
        if (a.stars >= 0 && b.stars >= 0) return a.stars < b.stars;
        const double da = a.objects / std::max(1.0, a.lengthMs) * (a.od + a.ar);
        const double db = b.objects / std::max(1.0, b.lengthMs) * (b.od + b.ar);
        return da < db;
    });
    return true;
}

void Library::sortSets() {
    std::sort(sets_.begin(), sets_.end(), [](const SetInfo &a, const SetInfo &b) {
        const std::string ka = fsu::toLower(a.artist + " " + a.title), kb = fsu::toLower(b.artist + " " + b.title);
        return ka < kb;
    });
}

void Library::loadCache() {
    cache_.clear();
    std::string text;
    if (!fsu::readText(root_ + "/" + kCacheName, text, 64u << 20)) return;
    size_t pos = 0;
    bool versionOk = false;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() == 2 && f[0] == "version") {
            versionOk = atoi(f[1].c_str()) == kCacheVersion;
            continue;
        }
        if (!versionOk) return;
        if (f.size() == 9 && f[0] == "S") {
            SetInfo s;
            s.folder = f[1];
            s.setId = atoi(f[2].c_str());
            s.title = f[3];
            s.titleUnicode = f[4];
            s.artist = f[5];
            s.artistUnicode = f[6];
            s.creator = f[7];
            s.tags = f[8];
            cache_[s.folder] = s;
        } else if (f.size() >= 19 && f[0] == "D") {
            auto it = cache_.find(f[1]);
            if (it == cache_.end()) continue;
            DiffInfo d;
            d.file = f[2];
            d.version = f[3];
            d.beatmapId = atoi(f[4].c_str());
            d.stars = static_cast<float>(atof(f[5].c_str()));
            d.ar = static_cast<float>(atof(f[6].c_str()));
            d.od = static_cast<float>(atof(f[7].c_str()));
            d.cs = static_cast<float>(atof(f[8].c_str()));
            d.hp = static_cast<float>(atof(f[9].c_str()));
            d.bpm = atof(f[10].c_str());
            d.lengthMs = atof(f[11].c_str());
            d.objects = atoi(f[12].c_str());
            d.circles = atoi(f[13].c_str());
            d.sliders = atoi(f[14].c_str());
            d.spinners = atoi(f[15].c_str());
            d.previewTime = atoi(f[16].c_str());
            d.audioFile = f[17];
            d.background = f[18];
            if (f.size() > 19) d.beatOffset = atof(f[19].c_str());
            it->second.diffs.push_back(d);
        }
    }
    LOG("library: cache de %zu sets charge", cache_.size());
}

void Library::saveCache() const {
    std::string text = "version\t" + std::to_string(kCacheVersion) + "\n";
    char buf[256];
    for (const auto &kv : cache_) {
        const SetInfo &s = kv.second;
        text += "S\t" + clean(s.folder) + "\t" + std::to_string(s.setId) + "\t" + clean(s.title) + "\t" +
                clean(s.titleUnicode) + "\t" + clean(s.artist) + "\t" + clean(s.artistUnicode) + "\t" + clean(s.creator) +
                "\t" + clean(s.tags) + "\n";
        for (const DiffInfo &d : s.diffs) {
            snprintf(buf, sizeof(buf), "\t%d\t%.3f\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f\t%.0f\t%d\t%d\t%d\t%d\t%d", d.beatmapId,
                     d.stars, d.ar, d.od, d.cs, d.hp, d.bpm, d.lengthMs, d.objects, d.circles, d.sliders, d.spinners,
                     d.previewTime);
            text += "D\t" + clean(s.folder) + "\t" + clean(d.file) + "\t" + clean(d.version) + buf + "\t" +
                    clean(d.audioFile) + "\t" + clean(d.background) + "\t" + std::to_string(static_cast<long long>(d.beatOffset)) + "\n";
        }
    }
    fsu::writeFileAtomic(root_ + "/" + kCacheName, text.data(), text.size());
}

void Library::saveFolderList() const {
    std::string text;
    for (const SetInfo &s : sets_) text += s.folder + "\n";
    fsu::writeFileAtomic(root_ + "/" + kFolderList, text.data(), text.size());
}

}  // namespace game
