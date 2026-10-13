// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "platform/platform.h"

namespace logx {
namespace {

constexpr size_t kStreamCap = 256 * 1024;  // historique gardé pour un client qui se connecte tard
constexpr size_t kRecentCap = 64;

std::mutex g_mutex;
FILE *g_file = nullptr;
std::string g_filePath;
char g_stream[kStreamCap];
uint64_t g_streamHead = 0;  // nombre total d'octets jamais écrits dans le flux
std::deque<std::string> g_recent;
double g_t0 = -1.0;

void appendStream(const char *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        g_stream[(g_streamHead + i) % kStreamCap] = data[i];
    }
    g_streamHead += len;
}

}  // namespace

void init(const char *path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_t0 < 0) g_t0 = plat::nowSeconds();
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
    g_filePath.clear();
    if (path && *path) {
        g_file = fopen(path, "w");
        if (g_file) {
            setvbuf(g_file, nullptr, _IONBF, 0);
            g_filePath = path;
        }
    }
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

const char *filePath() { return g_filePath.c_str(); }

void write(const char *fmt, ...) {
    char body[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_t0 < 0) g_t0 = plat::nowSeconds();
    const double t = plat::nowSeconds() - g_t0;

    char line[1100];
    int n = snprintf(line, sizeof(line), "[%9.3f] %s", t, body);
    if (n < 0) return;
    size_t len = static_cast<size_t>(n) < sizeof(line) - 2 ? static_cast<size_t>(n) : sizeof(line) - 2;
    // Une ligne par appel : on retire les retours à la ligne finaux puis on en remet un.
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) --len;
    line[len] = '\n';
    line[len + 1] = '\0';

    fputs(line, stdout);
    fflush(stdout);
    if (g_file) fputs(line, g_file);
    appendStream(line, len + 1);

    g_recent.emplace_back(line, len);
    while (g_recent.size() > kRecentCap) g_recent.pop_front();
}

std::vector<std::string> recent(size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t start = g_recent.size() > maxLines ? g_recent.size() - maxLines : 0;
    return std::vector<std::string>(g_recent.begin() + static_cast<long>(start), g_recent.end());
}

size_t readStream(uint64_t &cursor, char *dst, size_t cap, uint64_t &lost) {
    std::lock_guard<std::mutex> lock(g_mutex);
    lost = 0;
    const uint64_t oldest = g_streamHead > kStreamCap ? g_streamHead - kStreamCap : 0;
    if (cursor < oldest) {
        lost = oldest - cursor;
        cursor = oldest;
    }
    if (cursor > g_streamHead) cursor = g_streamHead;
    size_t n = static_cast<size_t>(g_streamHead - cursor);
    if (n > cap) n = cap;
    for (size_t i = 0; i < n; ++i) dst[i] = g_stream[(cursor + i) % kStreamCap];
    cursor += n;
    return n;
}

}  // namespace logx
