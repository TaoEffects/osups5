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

## État : étape 2 (jeu jouable, interface moderne)

| | |
| --- | --- |
| Gameplay osu!standard : cercles, sliders, spinners, empilement, score v1, précision, vie | ✅ |
| Mods : Easy, No Fail, Hidden, Hard Rock, Auto | ✅ |
| Musique MP3/OGG/WAV en flux, horloge audio précise, sons de frappe synthétisés | ✅ |
| Interface façon lazer : menu au rythme, sélection de map illustrée, résultats, options | ✅ |
| Téléchargement de maps dans l'appli (osu.direct), installation automatique | ✅ testé sur PC |
| Manette, clavier et souris dans toute l'interface | ✅ (souris : PC seulement pour l'instant) |
| 60 / 120 Hz (réglage dans les options) | ✅ |
| Scores locaux, correction automatique du décalage audio | ✅ |
| Test sur console de l'étape 2 | ⏳ voir [`docs/test-ps5.md`](docs/test-ps5.md) |

À venir : thèmes « classic » et « stable », étoiles calculées pour les maps importées à la main,
souris et tablette USB sur PS5, mods de vitesse (DT/HT), recherche au clavier virtuel de la console.

## Documents

- [`docs/verifications.md`](docs/verifications.md) : licences, audio, batching, entrées,
  maps/miroirs, mémoire, choix de la base (McOsu vs neomod).
- [`docs/test-ps5.md`](docs/test-ps5.md) : installer, lancer, quoi renvoyer.

## Organisation du code

```
src/platform/   platform.h + platform_ps5.cpp (EGL, ScePad, SceKeyboard, SceAudioOut)
                           + platform_sdl2.cpp (PC, pour développer sans console)
src/core/       log, log réseau, plantages, fichiers, réglages, JSON
src/render/     rendu par lots, texte TrueType (Inter), images, textures générées
src/audio/      mixeur, musique en flux, sons synthétisés
src/game/       beatmaps (.osu), courbes de sliders, gameplay, bibliothèque
src/net/        HTTPS (sceHttp sur PS5, libcurl sur PC), miroir osu.direct, .osz
src/ui/         écrans (menu, sélection, jeu, résultats, options, téléchargement)
src/app/        orchestration des écrans, entrées, fonds, scores
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
