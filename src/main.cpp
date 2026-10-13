// SPDX-License-Identifier: GPL-3.0-or-later
// Point d'entrée commun PS5 / PC. L'ordre est pensé pour le débogage à distance :
// le log réseau démarre AVANT la vidéo, pour qu'un plantage pendant l'init
// graphique laisse quand même une trace sur le PC.
#include <cstdio>
#include <cstring>

#include "app/game.h"
#include "core/settings.h"
#include "core/crash.h"
#include "core/log.h"
#include "core/netlog.h"
#include "core/thread.h"
#include "platform/platform.h"
#include "version.h"

namespace {

constexpr uint16_t kLogPort = 9025;

#if defined(__PROSPERO__)
constexpr double kWaitForLogClient = 3.0;  // laisse le temps au PC de se connecter
#else
constexpr double kWaitForLogClient = 0.0;
#endif

}  // namespace

int main() {
    char logPath[256];
    snprintf(logPath, sizeof(logPath), "%s/rhythm-ps5.log", plat::writableDir());
    logx::init(logPath);
    crash::install(logx::filePath());
    LOG("%s %s (rev %s, compile le %s %s) sur %s", RHYTHM_APP_NAME, RHYTHM_VERSION, RHYTHM_GIT_REV, __DATE__,
        __TIME__, plat::name());
    LOG("crash: ancre a %p (adresses de plantage relatives a rhythm_crash_anchor)",
        reinterpret_cast<void *>(crash::anchorAddress()));
    LOG("log fichier: %s", *logx::filePath() ? logx::filePath() : "(impossible a ouvrir)");

    thr::logStackInfo("principal");
    // Réglages lus tôt : fréquence d'affichage et journal réseau (option « Infos techniques »).
    Settings early;
    early.load(std::string(plat::writableDir()) + "/settings.cfg");
    if (early.showDebug) {
        netlog::start(kLogPort);
        if (kWaitForLogClient > 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "rhythm-ps5 : journal sur %s:%u", netlog::localAddress(), kLogPort);
            plat::notify(msg);
            netlog::waitForClient(kWaitForLogClient);
        }
    }

    plat::InitParams params;
    params.title = RHYTHM_APP_NAME;
    params.width = 1920;
    params.height = 1080;
    params.refreshHz = early.refreshHz;
    LOG("init video %dx%d @ %d Hz...", params.width, params.height, params.refreshHz);
    if (!plat::init(params)) {
        LOG("ECHEC init plateforme, arret");
        plat::sleepSeconds(1.0);
        return 1;
    }
    LOG("init video ok");

    Game *gamePtr = new Game();
    Game &app = *gamePtr;
    if (!app.init()) {
        LOG("ECHEC init application, arret");
        plat::sleepSeconds(1.0);
        return 1;
    }
    LOG("boucle principale");

    plat::InputState input;
    while (!app.wantsExit()) {
        plat::pollInput(input);
        app.frame(input, plat::nowSeconds());
        const double swapStart = plat::nowSeconds();
        plat::swapBuffers();
        app.reportSwap((plat::nowSeconds() - swapStart) * 1000);
    }

    LOG("sortie demandee");
    app.shutdown();
    delete gamePtr;
    netlog::stop();
    plat::requestExit();
    plat::shutdown();  // sur PS5 ne revient pas : sceSystemServiceLoadExec("exit")
    logx::shutdown();
    return 0;
}
