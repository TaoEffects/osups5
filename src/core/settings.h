// SPDX-License-Identifier: GPL-3.0-or-later
// Réglages persistants (fichier texte clé=valeur dans le dossier inscriptible).
#pragma once

#include <string>

struct Settings {
    // Affichage
    int refreshHz = 60;            // 60 ou 120 (appliqué au prochain lancement)
    float backgroundDim = 0.75f;   // assombrissement du fond en jeu (0..1)
    bool showDebug = false;
    bool showHitErrorBar = true;
    // Son
    float masterVolume = 0.8f;
    float musicVolume = 0.7f;
    float effectsVolume = 0.8f;
    int offsetMs = 0;              // décalage universel (positif = notes plus tard)
    // Entrées
    float cursorSpeed = 1400.0f;   // stick : pixels/s à fond (écran 1920 px)
    float cursorCurve = 1.6f;      // courbe de réponse du stick
    bool touchpadAbsolute = true;  // pavé tactile = zone absolue (comme une tablette)
    // Divers
    std::string lastSetFolder;     // dernière map sélectionnée
    int lastDifficulty = 0;
    std::string mirror = "https://osu.direct";
    bool firstRunDone = false;     // maps de base proposées au premier lancement
    int skin = 0;                  // style de jeu : 0 épuré (façon WhiteCat), 1 triangles (façon lazer)

    bool load(const std::string &path);
    bool save(const std::string &path) const;
};
