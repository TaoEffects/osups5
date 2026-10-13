// SPDX-License-Identifier: GPL-3.0-or-later
// Threads avec une taille de pile explicite : la taille par défaut d'un thread
// sur PS5 n'est pas documentée, on ne s'y fie pas.
#pragma once

#include <pthread.h>

#include <cstddef>
#include <cstdint>

namespace thr {

constexpr size_t kDefaultStack = 512 * 1024;

bool start(pthread_t &out, void *(*fn)(void *), void *arg, const char *name, size_t stackBytes = kDefaultStack);

// Écrit dans le log l'adresse de pile courante et les bornes de la pile du thread.
void logStackInfo(const char *who);

// Bornes de la pile du thread courant (0 si inconnues).
void stackBounds(uintptr_t &low, uintptr_t &high);

}  // namespace thr
