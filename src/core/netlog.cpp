// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/netlog.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/log.h"
#include "platform/platform.h"

namespace netlog {
namespace {

std::thread g_thread;
std::atomic<bool> g_running{false};
std::atomic<bool> g_connected{false};
std::atomic<int> g_clientFd{-1};
uint16_t g_port = 0;
char g_address[64] = "?";

// Trouve l'IP locale sans envoyer de paquet : un socket UDP "connecté" vers une
// adresse publique choisit l'interface de sortie, getsockname() la donne.
void detectLocalAddress() {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = htonl(0x08080808u);  // 8.8.8.8, jamais contacté
    if (connect(s, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
            inet_ntop(AF_INET, &local.sin_addr, g_address, sizeof(g_address));
        }
    }
    close(s);
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;  // pas de SIGPIPE si le PC coupe
#else
constexpr int kSendFlags = 0;
#endif

bool sendAll(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, data, len, kSendFlags);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void serveClient(int fd) {
    // Pas de fcntl() : dans le sandbox PS5 il échoue sur les sockets (EINVAL).
    // On reste en bloquant et on utilise poll() pour détecter la déconnexion.
    uint64_t cursor = 0;  // le nouveau client reçoit tout l'historique disponible
    char buf[8192];
    char hello[160];
    snprintf(hello, sizeof(hello), "=== log %s, connexion acceptee ===\n", plat::name());
    if (!sendAll(fd, hello, strlen(hello))) return;

    while (g_running.load()) {
        uint64_t lost = 0;
        size_t n = logx::readStream(cursor, buf, sizeof(buf), lost);
        if (lost) {
            char msg[96];
            snprintf(msg, sizeof(msg), "=== %llu octets de log perdus ===\n", static_cast<unsigned long long>(lost));
            if (!sendAll(fd, msg, strlen(msg))) return;
        }
        if (n > 0) {
            if (!sendAll(fd, buf, n)) return;
            continue;
        }
        // Rien à envoyer : on surveille le socket (fermeture côté PC) pendant 20 ms.
        pollfd p{fd, POLLIN, 0};
        int r = poll(&p, 1, 20);
        if (r > 0) {
            char junk[256];
            ssize_t got = recv(fd, junk, sizeof(junk), 0);
            if (got <= 0) return;  // le PC a fermé
        } else if (r < 0 && errno != EINTR) {
            return;
        }
    }
}

void serverLoop(int listenFd) {
    while (g_running.load()) {
        pollfd p{listenFd, POLLIN, 0};
        int r = poll(&p, 1, 100);
        if (r <= 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int fd = accept(listenFd, reinterpret_cast<sockaddr *>(&peer), &len);
        if (fd < 0) continue;
        char peerName[64] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, peerName, sizeof(peerName));
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof(one));
        g_clientFd.store(fd);
        g_connected.store(true);
        LOG("netlog: client %s connecte", peerName);
        serveClient(fd);
        g_clientFd.store(-1);
        close(fd);
        g_connected.store(false);
        LOG("netlog: client %s deconnecte", peerName);
    }
    close(listenFd);
}

}  // namespace

bool start(uint16_t port) {
    if (g_running.load()) return true;
    detectLocalAddress();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG("netlog: socket() a echoue errno=%d", errno);
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        LOG("netlog: bind(%u) a echoue errno=%d", port, errno);
        close(fd);
        return false;
    }
    if (listen(fd, 1) != 0) {
        LOG("netlog: listen() a echoue errno=%d", errno);
        close(fd);
        return false;
    }
    g_port = port;
    g_running.store(true);
    g_thread = std::thread(serverLoop, fd);
    LOG("netlog: en ecoute sur %s:%u (python3 tools/logrecv.py %s)", g_address, port, g_address);
    return true;
}

void stop() {
    if (!g_running.exchange(false)) return;
    if (g_thread.joinable()) g_thread.join();
}

bool clientConnected() { return g_connected.load(); }
int clientFd() { return g_clientFd.load(); }
uint16_t port() { return g_port; }
const char *localAddress() { return g_address; }

bool waitForClient(double timeoutSeconds) {
    const double end = plat::nowSeconds() + timeoutSeconds;
    while (!g_connected.load() && plat::nowSeconds() < end) plat::sleepSeconds(0.02);
    return g_connected.load();
}

}  // namespace netlog
