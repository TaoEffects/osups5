// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/i18n.h"

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace i18n {
namespace {

Lang g_lang = Lang::French;

struct Entry {
    const char *fr, *en;
};
const Entry kEnglish[] = {
#include "core/i18n_en.inc"
};

const std::unordered_map<std::string_view, const char *> &english() {
    static const std::unordered_map<std::string_view, const char *> map = [] {
        std::unordered_map<std::string_view, const char *> m;
        for (const Entry &e : kEnglish) m.emplace(e.fr, e.en);
        return m;
    }();
    return map;
}

}  // namespace

void setLang(Lang lang) { g_lang = lang; }
Lang lang() { return g_lang; }

Lang resolve(int setting, const char *systemLang) {
    if (setting == 1) return Lang::French;
    if (setting == 2) return Lang::English;
    return systemLang && !strncmp(systemLang, "fr", 2) ? Lang::French : Lang::English;
}

const char *tr(const char *french) {
    if (g_lang == Lang::French || !french || !*french) return french;
    const auto &m = english();
    const auto it = m.find(std::string_view(french));
    return it == m.end() ? french : it->second;
}

std::string tr(const std::string &french) {
    if (g_lang == Lang::French) return french;
    return tr(french.c_str());
}

}  // namespace i18n
