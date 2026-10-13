// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/beatmap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }

// --- Règles ----------------------------------------------------------------------------

double preemptMs(float ar) { return ar < 5 ? 1200.0 + 600.0 * (5 - ar) / 5 : 1200.0 - 750.0 * (ar - 5) / 5; }
double fadeInMs(float ar) { return ar < 5 ? 800.0 + 400.0 * (5 - ar) / 5 : 800.0 - 500.0 * (ar - 5) / 5; }
float circleRadius(float cs) { return 54.4f - 4.48f * cs; }
double hitWindow300(float od) { return 80.0 - 6.0 * od; }
double hitWindow100(float od) { return 140.0 - 8.0 * od; }
double hitWindow50(float od) { return 200.0 - 10.0 * od; }

// --- Lecture du texte ------------------------------------------------------------------

namespace {

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

bool keyValue(const std::string &line, std::string &key, std::string &value) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    key = trim(line.substr(0, colon));
    value = trim(line.substr(colon + 1));
    return true;
}

double num(const std::string &s) { return atof(s.c_str()); }
int inum(const std::string &s) { return atoi(s.c_str()); }

uint32_t packColor(int r, int g, int b) {
    return static_cast<uint32_t>(r & 255) | (static_cast<uint32_t>(g & 255) << 8) |
           (static_cast<uint32_t>(b & 255) << 16) | 0xFF000000u;
}

}  // namespace

bool parseBeatmap(const std::string &text, Beatmap &m, bool metadataOnly) {
    std::string section;
    size_t pos = 0;
    bool sawHeader = false;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!sawHeader) {
            // Retire un éventuel BOM UTF-8.
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
            if (line.rfind("osu file format v", 0) == 0) {
                m.formatVersion = inum(line.substr(17));
                sawHeader = true;
                continue;
            }
        }
        if (line.empty() || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) continue;
        if (line[0] == '[') {
            section = trim(line);
            if (metadataOnly && section == "[HitObjects]") break;
            continue;
        }
        std::string key, value;
        if (section == "[General]") {
            if (!keyValue(line, key, value)) continue;
            if (key == "AudioFilename") m.audioFile = value;
            else if (key == "AudioLeadIn") m.audioLeadIn = inum(value);
            else if (key == "PreviewTime") m.previewTime = inum(value);
            else if (key == "StackLeniency") m.stackLeniency = static_cast<float>(num(value));
            else if (key == "Mode") m.mode = inum(value);
        } else if (section == "[Metadata]") {
            if (!keyValue(line, key, value)) continue;
            if (key == "Title") m.title = value;
            else if (key == "TitleUnicode") m.titleUnicode = value;
            else if (key == "Artist") m.artist = value;
            else if (key == "ArtistUnicode") m.artistUnicode = value;
            else if (key == "Creator") m.creator = value;
            else if (key == "Version") m.version = value;
            else if (key == "Source") m.source = value;
            else if (key == "Tags") m.tags = value;
            else if (key == "BeatmapID") m.beatmapId = inum(value);
            else if (key == "BeatmapSetID") m.setId = inum(value);
        } else if (section == "[Difficulty]") {
            if (!keyValue(line, key, value)) continue;
            const float v = static_cast<float>(num(value));
            if (key == "HPDrainRate") m.hp = v;
            else if (key == "CircleSize") m.cs = v;
            else if (key == "OverallDifficulty") m.od = v;
            else if (key == "ApproachRate") m.ar = v;
            else if (key == "SliderMultiplier") m.sliderMultiplier = v;
            else if (key == "SliderTickRate") m.sliderTickRate = v;
        } else if (section == "[Events]") {
            const std::vector<std::string> f = split(line, ',');
            if (f.size() >= 3 && (f[0] == "0" || f[0] == "Background") && m.background.empty()) {
                std::string file = trim(f[2]);
                if (file.size() >= 2 && file.front() == '"') file = file.substr(1, file.size() - 2);
                m.background = file;
            } else if (f.size() >= 3 && (f[0] == "2" || f[0] == "Break")) {
                m.breaks.emplace_back(num(f[1]), num(f[2]));
            }
        } else if (section == "[TimingPoints]") {
            const std::vector<std::string> f = split(line, ',');
            if (f.size() < 2) continue;
            TimingPoint tp;
            tp.time = num(f[0]);
            tp.beatLength = num(f[1]);
            if (f.size() > 2) tp.meter = inum(f[2]);
            if (f.size() > 3) tp.sampleSet = inum(f[3]);
            if (f.size() > 4) tp.sampleIndex = inum(f[4]);
            if (f.size() > 5) tp.volume = inum(f[5]);
            tp.uninherited = f.size() > 6 ? inum(f[6]) != 0 : tp.beatLength > 0;
            if (f.size() > 7) tp.kiai = (inum(f[7]) & 1) != 0;
            if (std::isnan(tp.beatLength)) continue;
            m.timing.push_back(tp);
        } else if (section == "[Colours]") {
            if (!keyValue(line, key, value) || key.rfind("Combo", 0) != 0) continue;
            const std::vector<std::string> c = split(value, ',');
            if (c.size() >= 3) m.comboColors.push_back(packColor(inum(c[0]), inum(c[1]), inum(c[2])));
        } else if (section == "[HitObjects]") {
            const std::vector<std::string> f = split(line, ',');
            if (f.size() < 4) continue;
            HitObject o;
            o.pos = {static_cast<float>(num(f[0])), static_cast<float>(num(f[1]))};
            o.time = num(f[2]);
            const int type = inum(f[3]);
            o.newCombo = (type & 4) != 0;
            o.comboSkip = (type >> 4) & 7;
            o.hitSound = f.size() > 4 ? inum(f[4]) : 0;
            if (type & kSlider) {
                o.type = kSlider;
                if (f.size() < 8) continue;
                const std::vector<std::string> pts = split(f[5], '|');
                o.curve = pts.empty() || pts[0].empty() ? CurveType::Bezier : static_cast<CurveType>(pts[0][0]);
                o.controlPoints.push_back(o.pos);
                for (size_t i = 1; i < pts.size(); ++i) {
                    const size_t colon = pts[i].find(':');
                    if (colon == std::string::npos) continue;
                    o.controlPoints.push_back({static_cast<float>(num(pts[i].substr(0, colon))),
                                               static_cast<float>(num(pts[i].substr(colon + 1)))});
                }
                o.slides = std::max(1, inum(f[6]));
                o.pixelLength = num(f[7]);
                if (f.size() > 8)
                    for (const std::string &s : split(f[8], '|')) o.edgeSounds.push_back(inum(s));
                if (f.size() > 10) {
                    const std::vector<std::string> sets = split(f[10], ':');
                    if (sets.size() >= 2) {
                        o.normalSet = inum(sets[0]);
                        o.additionSet = inum(sets[1]);
                    }
                }
                ++m.sliders;
            } else if (type & kSpinner) {
                o.type = kSpinner;
                o.endTime = f.size() > 5 ? num(f[5]) : o.time;
                o.pos = {256, 192};
                if (f.size() > 6) {
                    const std::vector<std::string> sets = split(f[6], ':');
                    if (sets.size() >= 2) {
                        o.normalSet = inum(sets[0]);
                        o.additionSet = inum(sets[1]);
                    }
                }
                ++m.spinners;
            } else if (type & kCircle) {
                o.type = kCircle;
                o.endTime = o.time;
                if (f.size() > 5) {
                    const std::vector<std::string> sets = split(f[5], ':');
                    if (sets.size() >= 2) {
                        o.normalSet = inum(sets[0]);
                        o.additionSet = inum(sets[1]);
                    }
                }
                ++m.circles;
            } else {
                continue;  // osu!mania hold note etc.
            }
            m.objects.push_back(std::move(o));
        }
    }
    if (m.ar < 0) m.ar = m.od;  // anciennes maps : AR = OD
    if (m.titleUnicode.empty()) m.titleUnicode = m.title;
    if (m.artistUnicode.empty()) m.artistUnicode = m.artist;

    // BPM : min, max et principal (celui qui dure le plus longtemps).
    double lastTime = m.objects.empty() ? 0 : m.objects.back().time;
    double bestDur = -1;
    bool first = true;
    for (size_t i = 0; i < m.timing.size(); ++i) {
        const TimingPoint &tp = m.timing[i];
        if (!tp.uninherited || tp.beatLength <= 0) continue;
        const double bpm = 60000.0 / tp.beatLength;
        if (first || bpm < m.bpmMin) m.bpmMin = bpm;
        if (first || bpm > m.bpmMax) m.bpmMax = bpm;
        first = false;
        double next = lastTime;
        for (size_t j = i + 1; j < m.timing.size(); ++j)
            if (m.timing[j].uninherited) {
                next = m.timing[j].time;
                break;
            }
        const double dur = std::max(0.0, next - tp.time);
        if (dur > bestDur) {
            bestDur = dur;
            m.bpmMain = bpm;
        }
    }
    if (!m.objects.empty()) m.lengthMs = m.objects.back().time - m.objects.front().time;
    return sawHeader || !m.objects.empty() || !m.title.empty();
}

// --- Courbes des sliders ------------------------------------------------------------------

namespace {

constexpr float kBezierTolerance = 0.25f;
constexpr float kCircularTolerance = 0.1f;

bool bezierIsFlat(const std::vector<Vec2> &c) {
    for (size_t i = 1; i + 1 < c.size(); ++i) {
        const Vec2 d = c[i - 1] - c[i] * 2 + c[i + 1];
        if (d.x * d.x + d.y * d.y > kBezierTolerance * kBezierTolerance * 4) return false;
    }
    return true;
}

// Subdivision de de Casteljau (gauche/droite).
void bezierSubdivide(const std::vector<Vec2> &c, std::vector<Vec2> &l, std::vector<Vec2> &r) {
    const size_t n = c.size();
    std::vector<Vec2> mid(c);
    l.resize(n);
    r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = mid[0];
        r[n - i - 1] = mid[n - i - 1];
        for (size_t j = 0; j + 1 < n - i; ++j) mid[j] = (mid[j] + mid[j + 1]) * 0.5f;
    }
}

void bezierApproximate(const std::vector<Vec2> &c, std::vector<Vec2> &out) {
    // Approximation adaptative, comme l'osu-framework : on découpe jusqu'à ce que
    // chaque morceau soit plat, puis on échantillonne.
    std::vector<std::vector<Vec2>> stack;
    stack.push_back(c);
    std::vector<std::vector<Vec2>> flat;
    int guard = 0;
    while (!stack.empty() && guard++ < 20000) {
        std::vector<Vec2> part = std::move(stack.back());
        stack.pop_back();
        if (bezierIsFlat(part)) {
            flat.push_back(std::move(part));
            continue;
        }
        std::vector<Vec2> l, r;
        bezierSubdivide(part, l, r);
        stack.push_back(std::move(r));
        stack.push_back(std::move(l));
    }
    for (const std::vector<Vec2> &part : flat) {
        // Point du milieu par la formule de Bézier à t=0.5 suffisant sur un morceau plat.
        std::vector<Vec2> l, r;
        bezierSubdivide(part, l, r);
        out.push_back(part.front());
        out.push_back(l.back());
    }
    out.push_back(c.back());
}

void catmullApproximate(const std::vector<Vec2> &c, std::vector<Vec2> &out) {
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        const Vec2 v1 = i > 0 ? c[i - 1] : c[i];
        const Vec2 v2 = c[i];
        const Vec2 v3 = i + 1 < c.size() ? c[i + 1] : v2 + (v2 - v1);
        const Vec2 v4 = i + 2 < c.size() ? c[i + 2] : v3 + (v3 - v2);
        for (int k = 0; k < 50; ++k) {
            const float t = static_cast<float>(k) / 50.0f, t2 = t * t, t3 = t2 * t;
            out.push_back({0.5f * (2 * v2.x + (-v1.x + v3.x) * t + (2 * v1.x - 5 * v2.x + 4 * v3.x - v4.x) * t2 +
                                   (-v1.x + 3 * v2.x - 3 * v3.x + v4.x) * t3),
                           0.5f * (2 * v2.y + (-v1.y + v3.y) * t + (2 * v1.y - 5 * v2.y + 4 * v3.y - v4.y) * t2 +
                                   (-v1.y + 3 * v2.y - 3 * v3.y + v4.y) * t3)});
        }
    }
    out.push_back(c.back());
}

bool circularApproximate(const std::vector<Vec2> &c, std::vector<Vec2> &out) {
    const Vec2 a = c[0], b = c[1], cc = c[2];
    const float d = 2 * (a.x * (b.y - cc.y) + b.x * (cc.y - a.y) + cc.x * (a.y - b.y));
    if (std::fabs(d) < 1e-3f) return false;  // alignés : on retombe sur Bézier
    const float aSq = a.x * a.x + a.y * a.y, bSq = b.x * b.x + b.y * b.y, cSq = cc.x * cc.x + cc.y * cc.y;
    const Vec2 centre{(aSq * (b.y - cc.y) + bSq * (cc.y - a.y) + cSq * (a.y - b.y)) / d,
                      (aSq * (cc.x - b.x) + bSq * (a.x - cc.x) + cSq * (b.x - a.x)) / d};
    const Vec2 dA = a - centre, dC = cc - centre;
    const float r = length(dA);
    const double thetaStart = std::atan2(dA.y, dA.x);
    double thetaEnd = std::atan2(dC.y, dC.x);
    while (thetaEnd < thetaStart) thetaEnd += 2 * M_PI;
    double dir = 1, range = thetaEnd - thetaStart;
    // Sens : le point du milieu doit être du même côté que l'arc parcouru.
    Vec2 ortho{cc.y - a.y, -(cc.x - a.x)};
    if (ortho.x * (b.x - a.x) + ortho.y * (b.y - a.y) < 0) {
        dir = -1;
        range = 2 * M_PI - range;
    }
    const int points = r * 2 <= kCircularTolerance
                           ? 2
                           : std::max(2, static_cast<int>(std::ceil(range / (2 * std::acos(1 - kCircularTolerance / r)))));
    for (int i = 0; i < std::min(points, 1000); ++i) {
        const double theta = thetaStart + dir * static_cast<double>(i) / (points - 1) * range;
        out.push_back({centre.x + static_cast<float>(std::cos(theta)) * r, centre.y + static_cast<float>(std::sin(theta)) * r});
    }
    return true;
}

void buildPath(HitObject &o) {
    std::vector<Vec2> pts;
    const std::vector<Vec2> &cp = o.controlPoints;
    if (cp.size() < 2) {
        pts = {o.pos, o.pos};
    } else if (o.curve == CurveType::Linear) {
        pts = cp;
    } else if (o.curve == CurveType::Perfect && cp.size() == 3 && circularApproximate(cp, pts)) {
    } else if (o.curve == CurveType::Catmull) {
        catmullApproximate(cp, pts);
    } else {
        // Bézier par segments : un point répété (ancre rouge) coupe la courbe.
        std::vector<Vec2> seg;
        for (size_t i = 0; i < cp.size(); ++i) {
            seg.push_back(cp[i]);
            const bool split = i + 1 < cp.size() && cp[i].x == cp[i + 1].x && cp[i].y == cp[i + 1].y;
            if (split || i + 1 == cp.size()) {
                if (seg.size() >= 2) {
                    std::vector<Vec2> part;
                    bezierApproximate(seg, part);
                    if (!pts.empty()) pts.pop_back();
                    pts.insert(pts.end(), part.begin(), part.end());
                }
                seg.clear();
                if (split) seg.push_back(cp[i]);
                if (split) ++i;
            }
        }
        if (pts.size() < 2) pts = cp;
    }
    // Longueurs cumulées, puis ajustement à la longueur annoncée par la map.
    o.path.clear();
    o.cumLength.clear();
    float total = 0;
    o.path.push_back(pts[0]);
    o.cumLength.push_back(0);
    const float target = static_cast<float>(o.pixelLength);
    for (size_t i = 1; i < pts.size(); ++i) {
        const float seg = distance(pts[i - 1], pts[i]);
        if (seg < 1e-4f) continue;
        if (target > 0 && total + seg >= target) {
            const float k = (target - total) / seg;
            o.path.push_back(pts[i - 1] + (pts[i] - pts[i - 1]) * k);
            o.cumLength.push_back(target);
            total = target;
            break;
        }
        total += seg;
        o.path.push_back(pts[i]);
        o.cumLength.push_back(total);
    }
    if (target > 0 && total < target && o.path.size() >= 2) {
        // Chemin trop court : on prolonge le dernier segment.
        const Vec2 a = o.path[o.path.size() - 2], b = o.path.back();
        const float seg = distance(a, b);
        if (seg > 1e-4f) {
            o.path.push_back(b + (b - a) * ((target - total) / seg));
            o.cumLength.push_back(target);
        }
    }
    if (o.path.size() < 2) {
        o.path.push_back(o.path[0]);
        o.cumLength.push_back(0);
    }
}

}  // namespace

Vec2 HitObject::positionAt(float progress) const {
    if (path.empty()) return pos;
    const float total = cumLength.back();
    const float d = std::clamp(progress, 0.0f, 1.0f) * total;
    const auto it = std::lower_bound(cumLength.begin(), cumLength.end(), d);
    if (it == cumLength.begin()) return path.front();
    if (it == cumLength.end()) return path.back();
    const size_t i = static_cast<size_t>(it - cumLength.begin());
    const float seg = cumLength[i] - cumLength[i - 1];
    const float k = seg > 0 ? (d - cumLength[i - 1]) / seg : 0;
    return path[i - 1] + (path[i] - path[i - 1]) * k;
}

Vec2 HitObject::endPosition() const {
    if (type != kSlider) return pos;
    return positionAt(slides % 2 == 1 ? 1.0f : 0.0f);
}

Vec2 HitObject::ballPosition(double t) const {
    if (type != kSlider || spanDuration <= 0) return pos;
    double spans = (t - time) / spanDuration;
    spans = std::clamp(spans, 0.0, static_cast<double>(slides));
    int span = static_cast<int>(spans);
    if (span >= slides) span = slides - 1;
    double p = spans - span;
    if (span % 2 == 1) p = 1 - p;
    return positionAt(static_cast<float>(p));
}

// --- Préparation ---------------------------------------------------------------------------

void prepareBeatmap(Beatmap &m) {
    std::sort(m.timing.begin(), m.timing.end(), [](const TimingPoint &a, const TimingPoint &b) {
        if (a.time != b.time) return a.time < b.time;
        return a.uninherited && !b.uninherited;  // le rouge avant le vert au même instant
    });
    size_t ti = 0;
    double beatLength = 500, sv = 1;
    int sampleSet = 1, volume = 100;
    for (const TimingPoint &tp : m.timing)
        if (tp.uninherited) {
            beatLength = tp.beatLength;
            break;
        }
    int comboIndex = -1, comboNumber = 0;
    bool forceNewCombo = true;
    for (HitObject &o : m.objects) {
        while (ti < m.timing.size() && m.timing[ti].time <= o.time + 1) {
            const TimingPoint &tp = m.timing[ti];
            if (tp.uninherited) {
                beatLength = tp.beatLength > 0 ? tp.beatLength : beatLength;
                sv = 1;
            } else if (tp.beatLength < 0) {
                sv = std::clamp(-100.0 / tp.beatLength, 0.1, 10.0);
            }
            if (tp.sampleSet > 0) sampleSet = tp.sampleSet;
            volume = tp.volume;
            ++ti;
        }
        o.sampleSet = o.normalSet > 0 ? o.normalSet : sampleSet;
        o.volume = volume;
        if (o.type == kSlider) {
            buildPath(o);
            const double scoringDistance = 100.0 * m.sliderMultiplier * sv;
            const double velocity = scoringDistance / beatLength;  // px/ms
            o.spanDuration = velocity > 0 ? o.pixelLength / velocity : 0;
            o.endTime = o.time + o.spanDuration * o.slides;
            // Ticks : espacés de scoringDistance / tickRate (ancien format : sans SV).
            double tickDist = scoringDistance / std::max(0.1f, m.sliderTickRate);
            if (m.formatVersion < 8) tickDist /= sv;
            o.ticks.clear();
            const double minFromEnd = velocity * 10;
            if (tickDist > 1e-3 && o.pixelLength > 0) {
                for (double d = tickDist; d < o.pixelLength - minFromEnd && o.ticks.size() < 1000; d += tickDist) {
                    o.ticks.push_back({o.time + d / velocity, static_cast<float>(d / o.pixelLength)});
                }
            }
        } else if (o.type == kSpinner) {
            o.endTime = std::max(o.endTime, o.time);
        }
        // Combos : nouveau combo forcé après un spinner et au début.
        if (o.type == kSpinner) {
            forceNewCombo = true;
        } else {
            if (o.newCombo || forceNewCombo) {
                comboIndex += 1 + (forceNewCombo && !o.newCombo ? 0 : o.comboSkip);
                comboNumber = 0;
            }
            forceNewCombo = false;
            o.comboIndex = comboIndex < 0 ? 0 : comboIndex;
            o.comboNumber = ++comboNumber;
        }
    }

    // Empilement (version osu! v6+, comme lazer/McOsu).
    const double stackThreshold = preemptMs(m.ar) * m.stackLeniency;
    constexpr float kStackDistance = 3;
    std::vector<HitObject> &obj = m.objects;
    for (HitObject &o : obj) o.stackHeight = 0;
    for (int i = static_cast<int>(obj.size()) - 1; i > 0; --i) {
        int n = i;
        HitObject *objI = &obj[static_cast<size_t>(i)];
        if (objI->stackHeight != 0 || objI->type == kSpinner) continue;
        if (objI->type == kCircle) {
            while (--n >= 0) {
                HitObject *objN = &obj[static_cast<size_t>(n)];
                if (objN->type == kSpinner) continue;
                const double endTime = objN->type == kSlider ? objN->endTime : objN->time;
                if (objI->time - endTime > stackThreshold) break;
                if (objN->type == kSlider && distance(objN->endPosition(), objI->pos) < kStackDistance) {
                    const int offset = objI->stackHeight - objN->stackHeight + 1;
                    for (int j = n + 1; j <= i; ++j) {
                        HitObject &objJ = obj[static_cast<size_t>(j)];
                        if (distance(objN->endPosition(), objJ.pos) < kStackDistance) objJ.stackHeight -= offset;
                    }
                    break;
                }
                if (distance(objN->pos, objI->pos) < kStackDistance) {
                    objN->stackHeight = objI->stackHeight + 1;
                    objI = objN;
                }
            }
        } else if (objI->type == kSlider) {
            while (--n >= 0) {
                HitObject *objN = &obj[static_cast<size_t>(n)];
                if (objN->type == kSpinner) continue;
                if (objI->time - objN->time > stackThreshold) break;
                if (distance(objN->endPosition(), objI->pos) < kStackDistance) {
                    objN->stackHeight = objI->stackHeight + 1;
                    objI = objN;
                }
            }
        }
    }
    // Applique le décalage d'empilement aux positions et chemins.
    const float offsetUnit = circleRadius(m.cs) / 10.0f;
    for (HitObject &o : obj) {
        if (o.stackHeight == 0 || o.type == kSpinner) continue;
        const float off = -static_cast<float>(o.stackHeight) * offsetUnit;
        o.pos = o.pos + Vec2{off, off};
        for (Vec2 &p : o.path) p = p + Vec2{off, off};
    }
    if (!obj.empty()) m.lengthMs = obj.back().endTime - obj.front().time;
}

}  // namespace game
