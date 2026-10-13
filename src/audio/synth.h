// SPDX-License-Identifier: GPL-3.0-or-later
// Sons synthétisés au démarrage (pas de fichier audio à distribuer).
#pragma once

#include "audio/mixer.h"

namespace audio {

Sample makeHitSound();      // claquement percussif court (~70 ms)
Sample makeTick(float hz);  // clic de métronome (~30 ms)

}  // namespace audio
