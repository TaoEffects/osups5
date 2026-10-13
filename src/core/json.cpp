// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.h"

#include <cstdlib>

namespace json {
namespace {

const Value kNull;

struct Parser {
    const std::string &t;
    size_t p = 0;
    int depth = 0;
    std::string err;

    void ws() {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) ++p;
    }
    bool fail(const char *m) {
        if (err.empty()) err = m;
        return false;
    }
    static void utf8(std::string &out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    bool hex4(unsigned &v) {
        if (p + 4 > t.size()) return false;
        v = static_cast<unsigned>(strtoul(t.substr(p, 4).c_str(), nullptr, 16));
        p += 4;
        return true;
    }
    bool string(std::string &out) {
        if (t[p] != '"') return fail("chaine attendue");
        ++p;
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= t.size()) return fail("echappement");
            c = t[p++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return fail("\\u");
                if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                    p += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return fail("\\u");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += c; break;
            }
        }
        if (p >= t.size()) return fail("chaine non terminee");
        ++p;
        return true;
    }
    bool value(Value &v) {
        if (++depth > 64) return fail("trop profond");
        ws();
        if (p >= t.size()) return fail("fin inattendue");
        const char c = t[p];
        bool ok = true;
        if (c == '{') {
            v.type = Value::Object;
            ++p;
            ws();
            if (p < t.size() && t[p] == '}') {
                ++p;
            } else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!string(key)) return false;
                    ws();
                    if (p >= t.size() || t[p] != ':') return fail(": attendu");
                    ++p;
                    if (!value(v.o[key])) return false;
                    ws();
                    if (p < t.size() && t[p] == ',') {
                        ++p;
                        continue;
                    }
                    if (p < t.size() && t[p] == '}') {
                        ++p;
                        break;
                    }
                    return fail("} attendu");
                }
            }
        } else if (c == '[') {
            v.type = Value::Array;
            ++p;
            ws();
            if (p < t.size() && t[p] == ']') {
                ++p;
            } else {
                for (;;) {
                    v.a.emplace_back();
                    if (!value(v.a.back())) return false;
                    ws();
                    if (p < t.size() && t[p] == ',') {
                        ++p;
                        continue;
                    }
                    if (p < t.size() && t[p] == ']') {
                        ++p;
                        break;
                    }
                    return fail("] attendu");
                }
            }
        } else if (c == '"') {
            v.type = Value::String;
            ok = string(v.s);
        } else if (t.compare(p, 4, "true") == 0) {
            v.type = Value::Bool;
            v.b = true;
            p += 4;
        } else if (t.compare(p, 5, "false") == 0) {
            v.type = Value::Bool;
            p += 5;
        } else if (t.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            char *end = nullptr;
            v.n = strtod(t.c_str() + p, &end);
            if (end == t.c_str() + p) return fail("valeur invalide");
            v.type = Value::Number;
            p = static_cast<size_t>(end - t.c_str());
        }
        --depth;
        return ok;
    }
};

}  // namespace

const Value &Value::operator[](const std::string &key) const {
    if (type != Object) return kNull;
    auto it = o.find(key);
    return it == o.end() ? kNull : it->second;
}

const Value &Value::operator[](size_t i) const { return type == Array && i < a.size() ? a[i] : kNull; }

bool parse(const std::string &text, Value &out, std::string *error) {
    Parser parser{text, 0, 0, {}};
    out = Value{};
    const bool ok = parser.value(out);
    if (!ok && error) *error = parser.err;
    return ok;
}

}  // namespace json
