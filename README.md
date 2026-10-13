# rhythm-ps5

Jeu de rythme compatible avec les beatmaps osu!standard, pour **PS5 jailbreakée**
(homebrew). Interface inspirée d'osu!lazer, pensée pour la manette.

> Projet **non officiel**, sans lien avec ppy ni osu!. Il n'utilise ni le nom ni le logo
> osu! comme marque, n'inclut aucun asset officiel et **n'envoie jamais rien** aux
> serveurs officiels : les scores restent sur la console.
>
> Projet gratuit, développé **avec l'aide d'une IA** (Claude) et testé à la main sur
> console.

**Version alpha** : des bugs sont à prévoir.

## Fonctions

- Gameplay osu!standard : cercles, sliders, spinners, score, précision, vie.
- Mods : Easy, No Fail, Half Time, Hard Rock, Double Time, Nightcore, Hidden, Auto.
  DT / HT sans changer la hauteur du son.
- Interface façon lazer : intro, menu, sélection de map, mods, pause, résultats, options.
- Téléchargement de maps dans le jeu (miroir communautaire osu.direct), recherche au
  clavier virtuel de la console, métadonnées d'origine (japonais...).
- Skins osu! (.osk), deux styles intégrés (épuré, triangles).
- Manette (stick, pavé tactile), clavier et souris USB ; touches réglables.
- 60 / 120 Hz, scores locaux, correction automatique du décalage audio.

## Installation (PS5)

1. Télécharger `PPSA99731.zip` depuis les [releases](../../releases).
2. Décompresser le dossier `PPSA99731` sur la console (par FTP, par exemple dans
   `/data/homebrew/`) et l'enregistrer avec un gestionnaire d'applis en dossier
   (ShadowMountPlus, etc.).
3. Lancer l'appli. Au premier lancement, un assistant propose le tutoriel et des maps.

Dossiers sur la console :

| | |
| --- | --- |
| Maps | `/data/rhythm-ps5/songs` (dossiers ou `.osz`) |
| Skins | `/data/rhythm-ps5/skins` (dossiers ou `.osk`) |
| Sons personnels | `/data/rhythm-ps5/sons` |

## Commandes (manette)

| | |
| --- | --- |
| Croix | valider |
| Rond | retour |
| Croix directionnelle / stick gauche | naviguer |
| Stick droit | pointeur dans les menus (Croix pour cliquer) |
| Carré | mods (sélection), recherche (téléchargement) |
| Triangle | map au hasard |
| L1 / R1 | changer de page ou de tri |
| Options | options |

En jeu, les touches de frappe se règlent dans les options.

## Construire

```
bash ps5/build.sh                       # paquet PS5 -> build/ps5/dist/PPSA99731.zip
cmake -S . -B build/pc -G Ninja && ninja -C build/pc && ./build/pc/rhythm-ps5   # PC (SDL2)
```

## Licence

GPL-3.0-or-later (voir `LICENSE`). S'appuie sur
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl),
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPL-3.0+) et
[font8x8](https://github.com/dhepper/font8x8) (domaine public), les polices
[Outfit](https://github.com/Outfitio/Outfit-Fonts), [Inter](https://rsms.me/inter/) et [Noto Sans JP](https://fonts.google.com/noto/specimen/Noto+Sans+JP) (OFL) et les
icônes [Font Awesome Free](https://fontawesome.com) (police OFL, icônes CC BY 4.0). L'interface
reproduit le comportement d'[osu!lazer](https://github.com/ppy/osu) (MIT) d'après son code, sans
en reprendre d'asset. Les structures des
API Sony viennent du [fork SDL de ps5-payload-dev](https://github.com/ps5-payload-dev/SDL).

Musique du lancement : « triangles » de cYsmix, tirée de
[osu-resources](https://github.com/ppy/osu-resources) (© ppy Pty Ltd, licence
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)). Elle n'est **pas** incluse dans
ce dépôt ni dans l'archive : le jeu la télécharge une fois depuis ce dépôt public au premier
lancement, puis la garde dans `/data/rhythm-ps5/intro`.
