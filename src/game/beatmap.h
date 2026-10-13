// SPDX-License-Identifier: GPL-3.0-or-later
// Beatmaps au format .osu (osu!standard). Les règles (vitesse des sliders, ticks,
// combos, empilement) suivent osu!stable telles que reprises par McOsu et lazer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game {

struct Vec2 {
    float x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.y * k}; }
float length(Vec2 v);
float distance(Vec2 a, Vec2 b);

struct TimingPoint {
    double time = 0;
    double beatLength = 500;  // ms par temps (rouge) ou -100/multiplicateur (vert)
    int meter = 4;
    int sampleSet = 0;        // 0 = défaut, 1 normal, 2 soft, 3 drum
    int sampleIndex = 0;
    int volume = 100;
    bool uninherited = true;
    bool kiai = false;
};

enum ObjectType : int { kCircle = 1, kSlider = 2, kSpinner = 8 };
enum class CurveType : char { Linear = 'L', Perfect = 'P', Bezier = 'B', Catmull = 'C' };

struct SliderTick {
    double time;
    float progress;  // position sur le chemin, 0..1
};

struct HitObject {
    int type = kCircle;
    double time = 0, endTime = 0;
    Vec2 pos;
    bool newCombo = false;
    int comboSkip = 0;
    int hitSound = 0;     // bits : 1 whistle, 2 finish, 3 clap
    int normalSet = 0, additionSet = 0;
    // Slider
    CurveType curve = CurveType::Bezier;
    std::vector<Vec2> controlPoints;  // dont le point de départ
    int slides = 1;
    double pixelLength = 0;
    std::vector<int> edgeSounds;
    // Calculé par prepare()
    std::vector<Vec2> path;           // polyligne finale (coordonnées osu!, 512x384)
    std::vector<float> cumLength;
    double spanDuration = 0;
    std::vector<SliderTick> ticks;    // ticks de la première traversée
    int comboIndex = 0;               // pour la couleur
    int comboNumber = 1;
    int stackHeight = 0;
    // Timing effectif au moment de l'objet
    int sampleSet = 1;
    int volume = 100;

    Vec2 positionAt(float progress) const;  // progress 0..1 sur le chemin
    Vec2 endPosition() const;
    // Position de la bille à l'instant t (slides comprises).
    Vec2 ballPosition(double t) const;
};

struct Beatmap {
    int formatVersion = 14;
    std::string folder;   // dossier de la map
    std::string file;     // nom du .osu
    // [General]
    std::string audioFile;
    int audioLeadIn = 0;
    int previewTime = -1;
    float stackLeniency = 0.7f;
    int mode = 0;
    // [Metadata]
    std::string title, titleUnicode, artist, artistUnicode, creator, version, source, tags;
    int beatmapId = 0, setId = -1;
    // [Difficulty]
    float hp = 5, cs = 5, od = 5, ar = -1, sliderMultiplier = 1.4f, sliderTickRate = 1;
    // [Events]
    std::string background;
    std::vector<std::pair<double, double>> breaks;
    // [TimingPoints], [Colours], [HitObjects]
    std::vector<TimingPoint> timing;
    std::vector<uint32_t> comboColors;  // RGBA empaqueté
    std::vector<HitObject> objects;
    // Statistiques (remplies par parse)
    double bpmMin = 0, bpmMax = 0, bpmMain = 0;
    double lengthMs = 0;  // du premier au dernier objet
    int circles = 0, sliders = 0, spinners = 0;
};

// metadataOnly : s'arrête avant [HitObjects] (index de la bibliothèque, beaucoup plus rapide).
bool parseBeatmap(const std::string &text, Beatmap &out, bool metadataOnly);
// Calcule chemins, durées, ticks, combos et empilement.
void prepareBeatmap(Beatmap &map);

// Règles dérivées des paramètres de difficulté.
double preemptMs(float ar);
double fadeInMs(float ar);
float circleRadius(float cs);  // en pixels osu!
double hitWindow300(float od);
double hitWindow100(float od);
double hitWindow50(float od);

}  // namespace game
