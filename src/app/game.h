// SPDX-License-Identifier: GPL-3.0-or-later
// L'application : ressources partagées, écrans, transitions, entrées, fonds,
// musique d'aperçu et scores locaux.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/context.h"
#include "game/library.h"
#include "game/play.h"
#include "net/mirror.h"

class Game;

class Screen {
public:
    explicit Screen(Game &g) : game(g) {}
    virtual ~Screen() = default;
    virtual void enter() {}
    virtual void leave() {}
    virtual void update(const UiInput &in) = 0;
    virtual void render() = 0;
    // Écran de jeu : les boutons servent à frapper, pas à naviguer.
    virtual bool gameplay() const { return false; }

protected:
    Game &game;
};

struct LocalScore {
    std::string key;  // dossier/fichier de la difficulté
    long long score = 0;
    double accuracy = 0;
    int maxCombo = 0, n300 = 0, n100 = 0, n50 = 0, nMiss = 0;
    std::string rank, mods;
    long long date = 0;
};

class Game {
public:
    bool init();
    void frame(const plat::InputState &raw, double now);
    void shutdown();
    bool wantsExit() const { return exit_; }
    void requestExit() { exit_ = true; }

    Context ctx;

    // Navigation entre écrans (fondu enchaîné).
    void goTo(std::unique_ptr<Screen> next);
    std::unique_ptr<Screen> makeMainMenu();
    std::unique_ptr<Screen> makeSongSelect();
    std::unique_ptr<Screen> makePlay();
    std::unique_ptr<Screen> makeResults(const game::PlayResult &result);
    std::unique_ptr<Screen> makeOptions();
    std::unique_ptr<Screen> makeDownload();

    // Sélection courante.
    int selectedSet = -1, selectedDiff = 0;
    game::Mods mods;
    const game::SetInfo *currentSet() const;
    const game::DiffInfo *currentDiff() const;
    void select(int set, int diff, bool playPreview);

    // Fond flouté du set sélectionné (fondu entre l'ancien et le nouveau).
    void drawMenuBackground(float dim, float parallaxX = 0, float parallaxY = 0);
    // Vignette (image de fond réduite) d'un set, chargée à la demande ; nullptr si pas prête.
    const gfx::Texture *thumbnail(int setIndex);
    // Dernières entrées (pour les écrans qui en ont besoin au rendu).
    const UiInput &input() const { return lastInput_; }
    // Aperçu musical : boucle à partir du PreviewTime.
    void playPreview();

    // Scores locaux.
    std::vector<LocalScore> scoresFor(const std::string &key) const;
    void addScore(const LocalScore &s);
    static std::string diffKey(const game::SetInfo &set, const game::DiffInfo &diff);

    net::Mirror mirror;
    std::string lastToast;
    double toastTime = -10;
    void toast(const std::string &text);

private:
    UiInput buildInput(const plat::InputState &raw);
    void updateBackgroundLoads();
    void loadScores();
    void saveScores() const;

    gfx::SpriteBatch batch_;
    gfx::MsaaTarget msaa_;
    Fonts fonts_;
    Textures tex_;
    audio::Mixer mixer_;
    audio::Music music_;
    audio::SoundBank sfx_;
    Settings settings_;
    game::Library library_;
    gfx::ImageLoader images_;
    bool audioOk_ = false;

    std::unique_ptr<Screen> screen_, next_;
    double transitionStart_ = -10;
    bool exit_ = false;

    // Fonds
    gfx::Texture bgCurrent_, bgPrevious_;
    double bgFadeStart_ = -10;
    uint64_t bgTicket_ = 0;
    std::string bgPath_;
    std::string previewAudio_;

    // Entrées
    plat::InputState prev_;
    double repeatNext_ = 0;
    int repeatDir_ = 0;
    float cursorX_ = kVW / 2, cursorY_ = kVH / 2;
    float lastMouseX_ = -1, lastMouseY_ = -1;
    bool touchWasDown_ = false;
    bool mouseActive_ = false;
    bool msaaFailed_ = false;
    float touchLastX_ = 0, touchLastY_ = 0;
    double lastFrame_ = -1;
    double fpsAccum_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0;

    std::vector<LocalScore> scores_;
    struct Thumb {
        gfx::Texture tex;
        uint64_t ticket = 0;
        double lastUse = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, Thumb> thumbs_;
    std::unordered_map<uint64_t, std::string> thumbTickets_;
    UiInput lastInput_;
};
