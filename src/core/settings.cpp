// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/settings.h"

#include <cstdio>
#include <cstdlib>

#include "core/fsutil.h"
#include "core/log.h"

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

bool Settings::load(const std::string &path) {
    std::string text;
    if (!fsu::readText(path, text, 1 << 20)) return false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const float f = static_cast<float>(atof(value.c_str()));
        const int i = atoi(value.c_str());
        if (key == "refresh_hz") refreshHz = i == 120 ? 120 : 60;
        else if (key == "background_dim") backgroundDim = clampf(f, 0, 1);
        else if (key == "show_debug") showDebug = i != 0;
        else if (key == "hit_error_bar") showHitErrorBar = i != 0;
        else if (key == "master_volume") masterVolume = clampf(f, 0, 1);
        else if (key == "music_volume") musicVolume = clampf(f, 0, 1);
        else if (key == "effects_volume") effectsVolume = clampf(f, 0, 1);
        else if (key == "offset_ms") offsetMs = i < -300 ? -300 : (i > 300 ? 300 : i);
        else if (key == "cursor_speed") cursorSpeed = clampf(f, 200, 5000);
        else if (key == "cursor_curve") cursorCurve = clampf(f, 1, 3);
        else if (key == "touchpad_absolute") touchpadAbsolute = i != 0;
        else if (key == "last_set") lastSetFolder = value;
        else if (key == "last_difficulty") lastDifficulty = i;
        else if (key == "mirror" && !value.empty()) mirror = value;
    }
    LOG("settings: charges depuis %s (refresh %d Hz)", path.c_str(), refreshHz);
    return true;
}

bool Settings::save(const std::string &path) const {
    char buf[2048];
    const int n = snprintf(buf, sizeof(buf),
                           "refresh_hz=%d\nbackground_dim=%.2f\nshow_debug=%d\nhit_error_bar=%d\nmaster_volume=%.2f\n"
                           "music_volume=%.2f\neffects_volume=%.2f\noffset_ms=%d\ncursor_speed=%.0f\ncursor_curve=%.2f\n"
                           "touchpad_absolute=%d\nlast_set=%s\nlast_difficulty=%d\nmirror=%s\n",
                           refreshHz, backgroundDim, showDebug, showHitErrorBar, masterVolume, musicVolume,
                           effectsVolume, offsetMs, cursorSpeed, cursorCurve, touchpadAbsolute, lastSetFolder.c_str(),
                           lastDifficulty, mirror.c_str());
    if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return false;
    return fsu::writeFileAtomic(path, buf, static_cast<size_t>(n));
}
