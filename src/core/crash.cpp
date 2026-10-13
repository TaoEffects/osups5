// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  // REG_RIP sous Linux
#endif
#include "core/crash.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/netlog.h"

extern "C" __attribute__((noinline, used)) void rhythm_crash_anchor() { __asm__ volatile(""); }

namespace crash {
namespace {

int g_fd = -1;

void emit(const char *text) {
    const size_t len = strlen(text);
    ssize_t ignored = write(1, text, len);
    if (g_fd >= 0) ignored = write(g_fd, text, len);
    const int client = netlog::clientFd();
    if (client >= 0) ignored = send(client, text, len, 0);
    (void)ignored;
}

uintptr_t relative(uintptr_t address) { return address - anchorAddress(); }

void handler(int sig, siginfo_t *info, void *context) {
    char line[160];
    uintptr_t rip = 0, rbp = 0;
#if defined(__FreeBSD__)
    const ucontext_t *uc = static_cast<const ucontext_t *>(context);
    rip = static_cast<uintptr_t>(uc->uc_mcontext.mc_rip);
    rbp = static_cast<uintptr_t>(uc->uc_mcontext.mc_rbp);
#elif defined(__linux__) && defined(__x86_64__)
    const ucontext_t *uc = static_cast<const ucontext_t *>(context);
    rip = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    rbp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
#else
    (void)context;
#endif
    snprintf(line, sizeof(line), "\n!!! PLANTAGE signal=%d adresse=%p rip=ancre%+lld\n", sig,
             info ? info->si_addr : nullptr, static_cast<long long>(relative(rip)));
    emit(line);
    // Pile d'appels via les pointeurs de cadre (compilé avec -fno-omit-frame-pointer).
    for (int depth = 0; depth < 24 && rbp != 0 && (rbp & 7u) == 0; ++depth) {
        const uintptr_t *frame = reinterpret_cast<const uintptr_t *>(rbp);
        const uintptr_t ret = frame[1];
        if (ret == 0) break;
        snprintf(line, sizeof(line), "!!!   #%02d ancre%+lld\n", depth, static_cast<long long>(relative(ret)));
        emit(line);
        const uintptr_t next = frame[0];
        if (next <= rbp) break;  // la pile croît vers le bas : le cadre parent est plus haut
        rbp = next;
    }
    emit("!!! fin du rapport\n");
    _exit(128 + sig);
}

}  // namespace

uintptr_t anchorAddress() { return reinterpret_cast<uintptr_t>(&rhythm_crash_anchor); }

void install(const char *logFilePath) {
    if (logFilePath && *logFilePath) g_fd = open(logFilePath, O_WRONLY | O_APPEND);
    struct sigaction sa {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}

}  // namespace crash
