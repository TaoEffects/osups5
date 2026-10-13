// SPDX-License-Identifier: GPL-3.0-or-later
// Thème "moderne" (inspiré de lazer, sans aucun asset officiel) : couleurs,
// panneaux arrondis, icônes des boutons de la manette, fond animé.
// Les autres thèmes (classique, stable) viendront remplacer ce module.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "app/context.h"

namespace ui {

// Palette inspirée de celle de lazer (osu-framework / OsuColour).
namespace color {
constexpr uint32_t bg = gfx::rgba(22, 20, 28);
constexpr uint32_t panel = gfx::rgba(34, 30, 44, 235);
constexpr uint32_t panelLight = gfx::rgba(52, 46, 66, 240);
constexpr uint32_t accent = gfx::rgba(255, 102, 170);   // rose
constexpr uint32_t accent2 = gfx::rgba(102, 204, 255);  // bleu
constexpr uint32_t pink = gfx::rgba(255, 102, 170);
constexpr uint32_t pinkDark = gfx::rgba(204, 51, 119);
constexpr uint32_t purple = gfx::rgba(136, 102, 238);
constexpr uint32_t blue = gfx::rgba(102, 204, 255);
constexpr uint32_t yellow = gfx::rgba(255, 204, 34);
constexpr uint32_t green = gfx::rgba(136, 179, 0);
constexpr uint32_t lime = gfx::rgba(179, 217, 68);
constexpr uint32_t red = gfx::rgba(237, 17, 33);
constexpr uint32_t orange = gfx::rgba(255, 136, 51);
constexpr uint32_t text = gfx::rgba(255, 255, 255);
constexpr uint32_t textDim = gfx::rgba(190, 182, 210);
constexpr uint32_t good = gfx::rgba(140, 236, 140);
constexpr uint32_t warn = gfx::rgba(255, 200, 100);
constexpr uint32_t bad = gfx::rgba(255, 100, 100);
}  // namespace color

enum class Icon { Play, Download, Settings, Exit, Music, Clock, Star, Search, Back, Random, Mods, Circles, Sliders };

enum class PadIcon { Cross, Circle, Square, Triangle, L1, R1, L2, R2, Options, DPad, Stick };

void init(Context &ctx);
void shutdown();

// Rectangle aux coins arrondis.
void roundRect(Context &ctx, float x, float y, float w, float h, float radius, uint32_t color);
// Icône d'un bouton de manette (cercle de diamètre `size`).
void padIcon(Context &ctx, PadIcon icon, float cx, float cy, float size, float alpha = 1);
// Barre d'aide en bas d'écran : icônes + libellés.
void hintBar(Context &ctx, const std::vector<std::pair<PadIcon, const char *>> &hints, float alpha = 1);
// Fond : image (si fournie) floutée/assombrie + triangles animés.
void background(Context &ctx, const gfx::Texture *image, float dim, float parallaxX = 0, float parallaxY = 0);
// Icône vectorielle (dessinée au trait, aucune police d'icônes nécessaire).
void icon(Context &ctx, Icon icon, float cx, float cy, float size, uint32_t color);
// Bande colorée remplie de triangles animés (signature de lazer).
void trianglePanel(Context &ctx, float x, float y, float w, float h, uint32_t base, int seed, float alpha = 1);
// Parallélogramme incliné (boutons du menu et du pied de page de lazer).
void skewed(Context &ctx, float x, float y, float w, float h, float skew, uint32_t color);
// En-tête d'écran (bande à triangles + titre), façon "overlay" de lazer.
void header(Context &ctx, const char *title, const char *subtitle, uint32_t base, float alpha = 1);
// Flashs sur les bords de l'écran, au rythme.
void sideFlashes(Context &ctx, float intensity, uint32_t color);
// Visualiseur circulaire du spectre autour d'un point.
void visualiser(Context &ctx, float cx, float cy, float radius, float alpha);
// Curseur de menu (souris).
void cursor(Context &ctx, float x, float y, bool pressed);
// Bouton de bas d'écran : renvoie true si cliqué.
void footerButton(Context &ctx, float x, float w, const char *label, uint32_t color, PadIcon pad, bool hovered);
constexpr float kFooterH = 76;  // hauteur du pied de page
// Une même rangée de boutons doit être cliquable : position x de chacun.
std::vector<float> footerLayout(Context &ctx, const std::vector<const char *> &labels, float startX, std::vector<float> &widths);
uint32_t lighten(uint32_t c, float k);
uint32_t darken(uint32_t c, float k);

// Couleur selon le nombre d'étoiles (comme le dégradé de lazer).
uint32_t starColor(float stars);
// Formatage : "3:25".
std::string formatTime(double ms);

}  // namespace ui
