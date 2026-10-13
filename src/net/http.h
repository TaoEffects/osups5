// SPDX-License-Identifier: GPL-3.0-or-later
// Requêtes HTTPS GET. PS5 : services système sceHttp/sceSsl (magasin de certificats
// de la console, vérification active). PC : libcurl.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace net {

struct Response {
    int status = 0;
    std::vector<uint8_t> body;
    std::string error;  // vide si succès
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using Progress = std::function<void(uint64_t received, uint64_t total)>;
// Si fourni, reçoit les données au fil de l'eau (rien n'est gardé en mémoire).
// Renvoie false pour interrompre.
using Sink = std::function<bool(const uint8_t *data, size_t size)>;

bool init();
void shutdown();
// Bloquant : à appeler depuis un thread de travail.
Response get(const std::string &url, size_t maxBytes, const Progress &progress = nullptr,
             const std::atomic<bool> *cancel = nullptr, const Sink &sink = nullptr);
// Encode un paramètre d'URL.
std::string urlEncode(const std::string &s);

}  // namespace net
