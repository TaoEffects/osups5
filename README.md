# rhythm-ps5 (nom de travail)

Client de jeu de rythme compatible avec les beatmaps osu!, pour **PS5 jailbreakée**
(homebrew), dans l'esprit de McOsu/neosu mais pensé pour la console : interface à la
manette, DualSense, clavier/souris USB, téléchargement de maps via des miroirs.

> Projet non officiel, sans lien avec ppy ni osu!. Il n'utilise ni le nom ni le logo
> osu! comme marque, n'embarque aucun asset officiel et **n'envoie jamais de scores**
> aux serveurs officiels : les scores restent en local.
>
> Projet gratuit, non monétisé, développé **avec l'aide d'une IA** (Claude) ; les tests
> sur console sont faits à la main.

## État : étape 1 (squelette PS5)

| | |
|---|---|
| Fenêtre + OpenGL 3.3 core (EGL, ps5-opengl 1.0.1) | ✅ compile et se lie pour PS5, testé sur PC |
| Rendu 2D par lots (1 envoi de sommets / image) | ✅ |
| Son : mixeur 48 kHz sur `sceAudioOut`, horloge audio interpolée | ✅ |
| Manette DualSense (sticks, gâchettes, pavé tactile, barre lumineuse) | ✅ |
| Clavier USB | ✅ |
| Logs : fichier + écran de debug + **serveur TCP vers le PC** | ✅ |
| Rapport de plantage (pile d'appels → fichier:ligne) | ✅ testé sur PC |
| Sonde des dossiers (USB, disque interne, `/download0`) | ✅ |
| **Test sur console** | ⏳ à faire — voir [`docs/test-ps5.md`](docs/test-ps5.md) |

L'appli de l'étape 1 est un petit banc d'essai : des cercles tombent sur un tempo fixe
calé sur l'horloge audio ; on les frappe en rythme et l'appli mesure l'écart moyen,
les FPS, la régularité du son, l'état des entrées.

## Documents

- [`docs/verifications.md`](docs/verifications.md) : licences, audio, batching, entrées,
  maps/miroirs, mémoire, choix de la base (McOsu vs neomod).
- [`docs/test-ps5.md`](docs/test-ps5.md) : installer, lancer, quoi renvoyer.

## Organisation du code

```
src/platform/   platform.h + platform_ps5.cpp (EGL, ScePad, SceKeyboard, SceAudioOut)
                           + platform_sdl2.cpp (PC, pour développer sans console)
src/core/       log, serveur de log TCP, rapport de plantage, sonde des dossiers
src/render/     rendu par lots, police bitmap, textures générées
src/audio/      mixeur, sons synthétisés
src/app/        scène de l'étape 1
ps5/            build.sh (dossier d'appli PS5), deploy.sh (FTP), sce_sys/, assets/
tools/          logrecv.py (récepteur de logs), symbolize.py (plantages), make-icon.py
```

## Construire

```
bash ps5/build.sh                       # -> build/ps5/dist/PPSA99731/ et .zip
cmake -S . -B build/pc -G Ninja && ninja -C build/pc && ./build/pc/rhythm-ps5
```

Détails et prérequis dans [`docs/test-ps5.md`](docs/test-ps5.md).

## Licence

GPL-3.0-or-later (voir `LICENSE`). S'appuie sur
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl),
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPL-3.0+) et
[font8x8](https://github.com/dhepper/font8x8) (domaine public). Les structures des
API Sony viennent du [fork SDL de ps5-payload-dev](https://github.com/ps5-payload-dev/SDL).
