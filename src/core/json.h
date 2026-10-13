// SPDX-License-Identifier: GPL-3.0-or-later
// Lecteur JSON minimal (réponses des miroirs de beatmaps).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::map<std::string, Value> o;

    const Value &operator[](const std::string &key) const;
    const Value &operator[](size_t i) const;
    size_t size() const { return type == Array ? a.size() : (type == Object ? o.size() : 0); }
    double num(double def = 0) const { return type == Number ? n : def; }
    std::string str(const std::string &def = "") const { return type == String ? s : def; }
};

bool parse(const std::string &text, Value &out, std::string *error = nullptr);

}  // namespace json
