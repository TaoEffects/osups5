// SPDX-License-Identifier: GPL-3.0-or-later
// Rapport de plantage minimal. Sur SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT on écrit
// le signal, l'adresse fautive, RIP et une pile d'appels brute (chaîne de RBP)
// vers stdout, le fichier de log et le client réseau, puis on quitte.
// Les adresses sont relatives à la fonction `rhythm_crash_anchor` (son adresse est
// écrite au démarrage) : llvm-addr2line les traduit en fonctions/lignes avec
// l'ELF non strippé de la même build.
#pragma once

#include <cstdint>

extern "C" void rhythm_crash_anchor();

namespace crash {

void install(const char *logFilePath);
uintptr_t anchorAddress();

}  // namespace crash
