// SPDX-License-Identifier: GPL-3.0-or-later
// Serveur de log TCP. La console écoute sur un port ; le PC s'y connecte
// (tools/logrecv.py <ip-de-la-ps5>) et reçoit tout l'historique puis le direct.
// Choix d'un serveur côté console : Tao connaît déjà l'IP de la PS5 (il y envoie
// les payloads), il n'y a donc rien à configurer dans l'application.
#pragma once

#include <cstdint>

namespace netlog {

bool start(uint16_t port);
void stop();

bool clientConnected();
uint16_t port();
// Adresse IPv4 locale probable de la console (ou "?" si inconnue).
const char *localAddress();

// Descripteur du client connecté (-1 sinon). Réservé au rapport de plantage.
int clientFd();

// Attend qu'un client se connecte, au plus `timeoutSeconds`. Renvoie true si connecté.
bool waitForClient(double timeoutSeconds);

}  // namespace netlog
