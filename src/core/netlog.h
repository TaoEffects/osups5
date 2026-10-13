// SPDX-License-Identifier: GPL-3.0-or-later
// Journal par TCP (option « Infos techniques ») : la console écoute sur un port,
// un PC s'y connecte (par ex. `nc <ip-de-la-ps5> 9025`) et reçoit tout
// l'historique puis le direct. Rien à configurer dans l'application.
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
