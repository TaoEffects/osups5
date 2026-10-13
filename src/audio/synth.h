// SPDX-License-Identifier: GPL-3.0-or-later
// Sons synthétisés au démarrage : aucun fichier audio officiel n'est distribué.
#pragma once

#include <string>

#include "audio/mixer.h"

namespace audio {

Sample makeHitSound();      // claquement percussif court (~70 ms)
Sample makeTick(float hz);  // clic de métronome (~30 ms)

// Jeu complet de sons par défaut.
struct SoundBank {
    // [jeu 0=normal 1=soft 2=drum][0=hitnormal 1=whistle 2=finish 3=clap]
    Sample hit[3][4];
    Sample sliderTick;
    Sample sliderSlide;
    Sample spinnerBonus;
    Sample comboBreak;
    // Interface
    Sample uiHover, uiSelect, uiBack, uiOpen, uiError, uiDownloaded;
    void generate();
    // Remplace les sons par ceux trouvés dans `dir` (noms des skins osu! :
    // normal-hitnormal.wav, soft-hitclap.ogg, menuback.wav...). Renvoie le nombre chargé.
    int loadOverrides(const std::string &dir);
};

}  // namespace audio
