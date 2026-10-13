// SPDX-License-Identifier: GPL-3.0-or-later
// Journal unique de l'application. Chaque ligne part vers :
//  - stdout (sur PS5, le runtime ps5-opengl le redirige vers /download0/ps5-opengl.log),
//  - un fichier dédié, vidé à chaque ligne (survit à un plantage),
//  - un tampon circulaire lu par le serveur de log réseau (voir netlog.h),
//  - les dernières lignes affichées par l'écran de debug.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace logx {

void init(const char *filePath);
void shutdown();

void write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Dernières lignes (les plus anciennes d'abord), pour l'écran de debug.
std::vector<std::string> recent(size_t maxLines);

// Chemin du fichier effectivement ouvert ("" si aucun).
const char *filePath();

// Lecture du flux complet par le serveur réseau. `cursor` est une position absolue
// en octets ; la fonction copie au plus `cap` octets, avance le curseur et renvoie
// le nombre d'octets copiés. Si le lecteur a pris trop de retard, `lost` reçoit le
// nombre d'octets écrasés et le curseur saute au plus ancien octet disponible.
size_t readStream(uint64_t &cursor, char *dst, size_t cap, uint64_t &lost);

}  // namespace logx

#define LOG(...) ::logx::write(__VA_ARGS__)
