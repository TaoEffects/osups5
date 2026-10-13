// SPDX-License-Identifier: GPL-3.0-or-later
// Textures générées par le code : aucun asset officiel n'est embarqué.
#pragma once

#include "render/batch.h"

namespace gfx {

// Disque plein avec bord (cercle à frapper), blanc, à teinter par la couleur du sommet.
Texture makeHitCircle(int size);
// Anneau fin (cercle d'approche).
Texture makeRing(int size, float thicknessRatio);
// Point flou (curseur).
Texture makeSoftDot(int size);

}  // namespace gfx
