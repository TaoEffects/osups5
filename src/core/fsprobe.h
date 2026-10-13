// SPDX-License-Identifier: GPL-3.0-or-later
// Sonde des chemins de stockage au démarrage : quels dossiers existent, lesquels
// sont lisibles et inscriptibles depuis le sandbox de l'appli. Sert à décider où
// ranger les beatmaps (clé USB, disque interne, /download0...).
#pragma once

#include <string>
#include <vector>

namespace fsprobe {

struct Result {
    std::string path;
    bool exists = false;
    bool listable = false;
    int entries = -1;     // nombre d'entrées listées (plafonné), -1 si non listable
    bool writable = false;
    int error = 0;        // errno de la première opération en échec
};

std::vector<Result> run(const std::vector<std::string> &paths);

}  // namespace fsprobe
