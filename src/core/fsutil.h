// SPDX-License-Identifier: GPL-3.0-or-later
// Petits utilitaires de fichiers. Particularité PS5 : opendir() est refusé (EPERM)
// dans le sandbox d'une appli native (test du 13/10/2026), d'où le repli sur
// getdents() pour lister un dossier.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fsu {

struct Entry {
    std::string name;
    bool isDir = false;
};

bool readFile(const std::string &path, std::vector<uint8_t> &out, size_t maxBytes = 256u << 20);
bool readText(const std::string &path, std::string &out, size_t maxBytes = 16u << 20);
// Écrit dans un fichier temporaire puis le renomme : pas de fichier à moitié écrit.
bool writeFileAtomic(const std::string &path, const void *data, size_t size);
bool exists(const std::string &path);
bool isDir(const std::string &path);
bool makeDirs(const std::string &path);
bool removeFile(const std::string &path);
bool listDir(const std::string &path, std::vector<Entry> &out);

std::string join(const std::string &a, const std::string &b);
std::string extensionLower(const std::string &name);
std::string toLower(std::string s);
// Remplace les caractères interdits dans un nom de fichier.
std::string sanitizeName(const std::string &name);

}  // namespace fsu
