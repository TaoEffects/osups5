// SPDX-License-Identifier: GPL-3.0-or-later
// Changement de tempo sans changer la hauteur (Double Time / Half Time), par
// WSOLA : on recolle des fenêtres de 21 ms du signal d'origine, prises tous les
// `rate * hop` échantillons, en cherchant à ±7 ms le raccord le plus ressemblant
// pour éviter les « clics » et l'effet de phase.
#pragma once

#include <vector>

namespace audio {

class Stretcher {
public:
    static constexpr int kWindow = 1024, kHop = kWindow / 2, kTolerance = 320, kCorr = 512;

    void reset(double rate);
    void push(const float *stereo, int frames);
    // Fin du flux : complète avec du silence pour vider ce qui reste.
    void finish();
    // Copie au plus `frames` trames de sortie disponibles ; renvoie le nombre copié.
    int pull(float *stereo, int frames);
    int available() const { return static_cast<int>(out_.size() / 2 - outRead_); }

private:
    void process();
    bool hop();

    double rate_ = 1;
    std::vector<float> in_;      // stéréo
    std::vector<float> mono_;    // somme L+R, pour la corrélation
    double aPos_ = 0;            // position d'analyse (trames, relative à in_[0])
    int prev_ = -1;              // début du segment précédent (-1 : aucun)
    std::vector<float> olap_ = std::vector<float>(kWindow * 2, 0.0f);
    std::vector<float> out_;
    size_t outRead_ = 0;
    std::vector<float> window_;
};

}  // namespace audio
