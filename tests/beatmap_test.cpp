// Vérifie le parseur sur les .osu passés en argument.
#include <cmath>
#include <cstdio>

#include "core/fsutil.h"
#include "game/beatmap.h"

int main(int argc, char **argv) {
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        std::string text;
        if (!fsu::readText(argv[i], text)) {
            printf("illisible: %s\n", argv[i]);
            ++failures;
            continue;
        }
        game::Beatmap m;
        if (!game::parseBeatmap(text, m, false)) {
            printf("echec: %s\n", argv[i]);
            ++failures;
            continue;
        }
        game::prepareBeatmap(m);
        int badPaths = 0, stacked = 0;
        double maxErr = 0;
        for (const game::HitObject &o : m.objects) {
            if (o.stackHeight) ++stacked;
            if (o.type != game::kSlider) continue;
            if (o.path.size() < 2) ++badPaths;
            const double err = std::fabs(o.cumLength.back() - o.pixelLength);
            if (err > maxErr) maxErr = err;
        }
        printf("%s - %s [%s] v%d : %zu objets (%d/%d/%d) bpm %.0f-%.0f (%.0f) duree %.0fs AR%.1f OD%.1f CS%.1f "
               "fond='%s' audio='%s' empiles=%d chemins_invalides=%d ecart_longueur_max=%.3f\n",
               m.artist.c_str(), m.title.c_str(), m.version.c_str(), m.formatVersion, m.objects.size(), m.circles,
               m.sliders, m.spinners, m.bpmMin, m.bpmMax, m.bpmMain, m.lengthMs / 1000, m.ar, m.od, m.cs,
               m.background.c_str(), m.audioFile.c_str(), stacked, badPaths, maxErr);
        if (badPaths || maxErr > 1.0 || m.objects.empty()) ++failures;
    }
    return failures ? 1 : 0;
}
