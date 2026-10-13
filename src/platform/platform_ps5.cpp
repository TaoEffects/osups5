// SPDX-License-Identifier: GPL-3.0-or-later
// Plateforme PS5 (application native en dossier, lancée comme un titre installé).
//  - vidéo : EGL + OpenGL 3.3 core du SDK ps5-opengl (Mesa sur le vrai GPU) ;
//  - manette : ScePad (lecture de l'état courant à chaque image) ;
//  - clavier USB : SceKeyboard ;
//  - audio : SceAudioOut, 48 kHz, thread dédié qui bloque sur sceAudioOutOutput.
// Les structures Sony ci-dessous reprennent celles du fork SDL de ps5-payload-dev
// (src/joystick/ps5/SDL_ps5joystick.h, src/video/ps5/SDL_ps5keyboard.c), déjà
// utilisées par plusieurs portages ; les static_assert en verrouillent la taille.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ps5_opengl_display_modes.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "core/log.h"
#include "platform/platform.h"
#include "render/gl.h"

namespace {

// --- ABI Sony (sous-ensemble) ----------------------------------------------------

struct SonyPadTouch {
    uint16_t x;
    uint16_t y;
    uint8_t finger;
    uint8_t pad[3];
};

struct SonyPadData {
    uint32_t buttons;
    uint8_t leftX, leftY;
    uint8_t rightX, rightY;
    uint8_t l2, r2;
    uint16_t padding;
    float quat[4];
    float velocity[3];
    float acceleration[3];
    uint8_t touchFingers;
    uint8_t touchPad1[3];
    uint32_t touchPad2;
    SonyPadTouch touch[2];
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t connectedCount;
    uint8_t unknown[15];
};
static_assert(sizeof(SonyPadData) == 120, "taille de l'etat manette");
static_assert(offsetof(SonyPadData, touchFingers) == 0x34, "offset pave tactile");
static_assert(offsetof(SonyPadData, connected) == 0x4c, "offset connexion manette");

struct SonyPadColor {
    uint8_t r, g, b, a;
};

enum : uint32_t {
    kSonyL3 = 0x0002,
    kSonyR3 = 0x0004,
    kSonyOptions = 0x0008,
    kSonyUp = 0x0010,
    kSonyRight = 0x0020,
    kSonyDown = 0x0040,
    kSonyLeft = 0x0080,
    kSonyL2 = 0x0100,
    kSonyR2 = 0x0200,
    kSonyL1 = 0x0400,
    kSonyR1 = 0x0800,
    kSonyTriangle = 0x1000,
    kSonyCircle = 0x2000,
    kSonyCross = 0x4000,
    kSonySquare = 0x8000,
    kSonyTouchpad = 0x100000,
    kSonyIntercepted = 0x80000000,  // l'interface système a le focus
};

struct SonyKeyboardData {
    uint64_t junk0[2];
    uint8_t available;
    uint32_t junk1[2];
    uint32_t modifiers;
    uint16_t scankey[16];
    uint64_t junk3[4];
};
static_assert(offsetof(SonyKeyboardData, modifiers) == 28, "offset modificateurs clavier");
static_assert(offsetof(SonyKeyboardData, scankey) == 32, "offset touches clavier");

struct SonyNotification {
    uint8_t reserved[45];
    char message[3075];
};

constexpr int kAudioPortMain = 0;
constexpr int kAudioFormatS16Stereo = 1;
constexpr int kUserSystem = 0xFF;

}  // namespace

extern "C" {
int sceUserServiceInitialize(void *);
int sceUserServiceGetInitialUser(int *);
int scePadInit(void);
int scePadOpen(int user, int type, int index, const void *param);
int scePadReadState(int handle, SonyPadData *data);
int scePadSetLightBar(int handle, const SonyPadColor *color);
int scePadClose(int handle);
int sceKeyboardInit(void);
int sceKeyboardOpen(int user, int type, int index, void *param);
int sceKeyboardReadState(int handle, SonyKeyboardData *data);
int sceKeyboardClose(int handle);
int sceAudioOutInit(void);
int sceAudioOutOpen(int user, int type, int index, uint32_t len, uint32_t freq, uint32_t param);
int sceAudioOutOutput(int handle, const void *data);
int sceAudioOutClose(int handle);
int sceSystemServiceLoadExec(const char *path, char *const argv[]);
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
}

namespace plat {
namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
int g_width = 0, g_height = 0, g_refresh = 0;
bool g_refreshQueried = false;

int g_user = -1;
int g_pad = -1;
int g_keyboard = -1;
double g_nextPadRetry = 0;
bool g_padWasConnected = false;
uint16_t g_touchMaxX = 0, g_touchMaxY = 0;

std::thread g_audioThread;
std::atomic<bool> g_audioRunning{false};
int g_audioHandle = -1;
int g_audioBlock = 0;

bool g_exitRequested = false;

bool eglCheck(bool ok, const char *stage) {
    if (!ok) LOG("ps5: ECHEC %s (egl=0x%x)", stage, eglGetError());
    return ok;
}

void openPad() {
    if (g_user < 0) return;
    g_pad = scePadOpen(g_user, 0, 0, nullptr);
    if (g_pad < 0) {
        LOG("ps5: scePadOpen a echoue: 0x%08x", static_cast<unsigned>(g_pad));
        return;
    }
    LOG("ps5: manette ouverte (handle %d)", g_pad);
    const SonyPadColor color{255, 60, 160, 255};  // barre lumineuse rose : l'appli tourne
    scePadSetLightBar(g_pad, &color);
}

float stick(uint8_t v) {
    const float f = (static_cast<float>(v) - 127.5f) / 127.5f;
    return f < -1 ? -1 : (f > 1 ? 1 : f);
}

void audioLoop(AudioCallback cb, void *user) {
    std::vector<float> mixBuffer(static_cast<size_t>(g_audioBlock) * 2);
    std::vector<int16_t> outBuffer(static_cast<size_t>(g_audioBlock) * 2);
    while (g_audioRunning.load(std::memory_order_relaxed)) {
        cb(mixBuffer.data(), g_audioBlock, user);
        for (size_t i = 0; i < mixBuffer.size(); ++i) {
            float v = mixBuffer[i] * 32767.0f;
            v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
            outBuffer[i] = static_cast<int16_t>(v);
        }
        // Bloque jusqu'à ce que le matériel accepte le bloc : c'est notre cadence.
        const int r = sceAudioOutOutput(g_audioHandle, outBuffer.data());
        if (r < 0) {
            LOG("ps5: sceAudioOutOutput a echoue: 0x%08x, arret du thread audio", static_cast<unsigned>(r));
            break;
        }
    }
}

}  // namespace

bool init(const InitParams &params) {
    // Services utilisateur (manette, clavier) : sans eux, scePadOpen échoue.
    int r = sceUserServiceInitialize(nullptr);
    LOG("ps5: sceUserServiceInitialize = 0x%08x", static_cast<unsigned>(r));
    r = sceUserServiceGetInitialUser(&g_user);
    LOG("ps5: utilisateur initial = %d (0x%08x)", g_user, static_cast<unsigned>(r));
    r = scePadInit();
    LOG("ps5: scePadInit = 0x%08x", static_cast<unsigned>(r));
    openPad();
    r = sceKeyboardInit();
    LOG("ps5: sceKeyboardInit = 0x%08x", static_cast<unsigned>(r));
    {
        int junk = 0;
        g_keyboard = sceKeyboardOpen(g_user, 0, 0, &junk);
        LOG("ps5: sceKeyboardOpen = %d", g_keyboard);
    }

    // Vidéo : le mode d'affichage se choisit AVANT eglInitialize.
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                       EGL_NONE};
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                        EGL_NONE};
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglCheck(g_display != EGL_NO_DISPLAY, "eglGetDisplay")) return false;
    if (!eglSetDisplayModePS5(g_display, params.width, params.height)) {
        LOG("ps5: mode %dx%d refuse, on garde 1920x1080", params.width, params.height);
    }
    if (!eglSetDisplayRefreshPS5(g_display, params.refreshHz)) {
        LOG("ps5: frequence %d Hz refusee", params.refreshHz);
    }
    EGLint major = 0, minor = 0;
    if (!eglCheck(eglInitialize(g_display, &major, &minor), "eglInitialize")) return false;
    LOG("ps5: EGL %d.%d", major, minor);
    if (!eglCheck(eglBindAPI(EGL_OPENGL_API), "eglBindAPI")) return false;
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglCheck(eglChooseConfig(g_display, configAttributes, &config, 1, &count) && count == 1, "eglChooseConfig"))
        return false;
    g_surface = eglCreateWindowSurface(g_display, config, static_cast<EGLNativeWindowType>(0), nullptr);
    if (!eglCheck(g_surface != EGL_NO_SURFACE, "eglCreateWindowSurface")) return false;
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, contextAttributes);
    if (!eglCheck(g_context != EGL_NO_CONTEXT, "eglCreateContext")) return false;
    if (!eglCheck(eglMakeCurrent(g_display, g_surface, g_surface, g_context), "eglMakeCurrent")) return false;
    eglSwapInterval(g_display, 1);
    EGLint w = 0, h = 0;
    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &w);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &h);
    g_width = w > 0 ? w : params.width;
    g_height = h > 0 ? h : params.height;
    LOG("ps5: surface %dx%d", g_width, g_height);
    return true;
}

void shutdown() {
    audioStop();
    if (g_pad >= 0) scePadClose(g_pad);
    if (g_keyboard >= 0) sceKeyboardClose(g_keyboard);
    g_pad = g_keyboard = -1;
    if (g_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
        if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
        eglTerminate(g_display);
    }
    g_display = EGL_NO_DISPLAY;
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    if (g_exitRequested) {
        LOG("ps5: fermeture via sceSystemServiceLoadExec(\"exit\")");
        logx::shutdown();
        sceSystemServiceLoadExec("exit", nullptr);
    }
}

const char *name() { return "ps5"; }
int width() { return g_width; }
int height() { return g_height; }

int refreshHz() {
    // La fréquence acceptée par la sortie n'est définitive qu'après la 1re image.
    if (!g_refreshQueried && g_display != EGL_NO_DISPLAY) {
        EGLint rw = 0, rh = 0, hz = 0;
        if (eglGetDisplayModePS5(g_display, &rw, &rh, &hz)) {
            g_refresh = hz;
            g_refreshQueried = true;
            LOG("ps5: mode d'affichage effectif %dx%d @ %d Hz", rw, rh, hz);
        }
    }
    return g_refresh;
}

void pollInput(InputState &out) {
    PadState &p = out.pad;
    p = PadState{};
    if (g_pad < 0 && nowSeconds() >= g_nextPadRetry) {
        g_nextPadRetry = nowSeconds() + 2.0;
        openPad();
    }
    if (g_pad >= 0) {
        SonyPadData d{};
        const int r = scePadReadState(g_pad, &d);
        p.connected = r == 0 && d.connected;
        if (p.connected != g_padWasConnected) {
            LOG("ps5: manette %s", p.connected ? "connectee" : "deconnectee");
            g_padWasConnected = p.connected;
        }
        // Si l'interface système a le focus (bouton PS), on ignore les entrées.
        if (p.connected && !(d.buttons & kSonyIntercepted)) {
            const uint32_t b = d.buttons;
            p.rawButtons = b;
            struct Map {
                uint32_t sony, ours;
            };
            static const Map map[] = {
                {kSonyCross, kPadCross},       {kSonyCircle, kPadCircle}, {kSonySquare, kPadSquare},
                {kSonyTriangle, kPadTriangle}, {kSonyL1, kPadL1},         {kSonyR1, kPadR1},
                {kSonyL2, kPadL2},             {kSonyR2, kPadR2},         {kSonyL3, kPadL3},
                {kSonyR3, kPadR3},             {kSonyUp, kPadUp},         {kSonyDown, kPadDown},
                {kSonyLeft, kPadLeft},         {kSonyRight, kPadRight},   {kSonyOptions, kPadOptions},
                {kSonyTouchpad, kPadTouchpad},
            };
            for (const Map &m : map)
                if (b & m.sony) p.buttons |= m.ours;
            p.lx = stick(d.leftX);
            p.ly = stick(d.leftY);
            p.rx = stick(d.rightX);
            p.ry = stick(d.rightY);
            p.l2 = static_cast<float>(d.l2) / 255.0f;
            p.r2 = static_cast<float>(d.r2) / 255.0f;
            p.touchCount = d.touchFingers > 2 ? 2 : d.touchFingers;
            for (int i = 0; i < p.touchCount; ++i) {
                p.touchX[i] = d.touch[i].x;
                p.touchY[i] = d.touch[i].y;
                if (d.touch[i].x > g_touchMaxX || d.touch[i].y > g_touchMaxY) {
                    g_touchMaxX = d.touch[i].x > g_touchMaxX ? d.touch[i].x : g_touchMaxX;
                    g_touchMaxY = d.touch[i].y > g_touchMaxY ? d.touch[i].y : g_touchMaxY;
                }
            }
            p.timestampUs = d.timestamp;
        }
    }

    KeyboardState &k = out.keyboard;
    k = KeyboardState{};
    if (g_keyboard >= 0) {
        SonyKeyboardData d{};
        if (sceKeyboardReadState(g_keyboard, &d) == 0 && d.available) {
            k.connected = true;
            for (uint16_t code : d.scankey)
                if (code > 0 && code < 256) k.keys[code] = true;
            for (int bit = 0; bit < 8; ++bit)  // Ctrl, Maj, Alt, Gui gauche puis droite
                if (d.modifiers & (1u << bit)) k.keys[0xE0 + bit] = true;
        }
    }
    out.mouse = MouseState{};  // pas de bouchon libSceMouse dans le SDK public (étape 3)
    out.quitRequested = g_exitRequested;
}

void swapBuffers() { eglSwapBuffers(g_display, g_surface); }

double nowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

void sleepSeconds(double seconds) {
    if (seconds > 0) usleep(static_cast<useconds_t>(seconds * 1e6));
}

bool audioStart(AudioCallback cb, void *user, int framesPerBlock) {
    int r = sceAudioOutInit();
    LOG("ps5: sceAudioOutInit = 0x%08x", static_cast<unsigned>(r));
    // Tailles de bloc acceptées : multiples de 256 jusqu'à 2048.
    int block = (framesPerBlock + 255) / 256 * 256;
    block = block < 256 ? 256 : (block > 2048 ? 2048 : block);
    g_audioHandle = sceAudioOutOpen(kUserSystem, kAudioPortMain, 0, static_cast<uint32_t>(block), kAudioRate,
                                    kAudioFormatS16Stereo);
    if (g_audioHandle < 0) {
        LOG("ps5: sceAudioOutOpen a echoue: 0x%08x", static_cast<unsigned>(g_audioHandle));
        return false;
    }
    g_audioBlock = block;
    LOG("ps5: audio ouvert (handle %d), %d Hz, blocs de %d trames (%.2f ms)", g_audioHandle, kAudioRate, block,
        1000.0 * block / kAudioRate);
    g_audioRunning.store(true);
    g_audioThread = std::thread(audioLoop, cb, user);
    return true;
}

void audioStop() {
    if (g_audioRunning.exchange(false) && g_audioThread.joinable()) g_audioThread.join();
    if (g_audioHandle >= 0) sceAudioOutClose(g_audioHandle);
    g_audioHandle = -1;
}

int audioBlockFrames() { return g_audioBlock; }

const char *writableDir() { return "/download0"; }
const char *assetsDir() { return "/app0/assets"; }

void notify(const char *message) {
    static SonyNotification request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.message, sizeof(request.message), "%s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void requestExit() { g_exitRequested = true; }

}  // namespace plat
