# Tester sur la PS5

But du test : vérifier sur ta console que l'appli **démarre**, **affiche**, **joue du
son**, **lit la manette**, et **renvoie ses logs** au PC. Les plantages étant souvent
silencieux, tout est prévu pour qu'on récupère un maximum d'informations même si ça
plante.

## Ce qu'il te faut

- Console avec Relapse + HEN (etaHEN / kstuff…) comme d'habitude.
- Un **serveur FTP** sur la console (celui fourni par ton HEN, port 2121 en général).
- Un **chargeur de titres en dossier** : le boilerplate est validé avec
  **ShadowMountPlus** (github.com/drakmor/ShadowMountPlus). Il surveille
  `/data/homebrew/` et fait apparaître les titres sur l'écran d'accueil.
- Sur le PC : Python 3 (pour recevoir les logs).

> Attention : ce n'est **pas** un payload ELF à envoyer sur le port 9021. ps5-opengl
> exige un titre installé (voir `docs/verifications.md`).

## 1. Installer l'appli

Option A — à la main (FileZilla ou autre client FTP) :
1. Décompresse `PPSA99731.zip` sur le PC : tu obtiens un dossier `PPSA99731/`.
2. Copie **le dossier entier** dans `/data/homebrew/` de la console :
   le résultat doit être `/data/homebrew/PPSA99731/eboot.bin` (+ `sce_sys/`, `sce_module/`, `assets/`).
   Ne copie pas le .zip lui-même.

Option B — depuis Linux/WSL avec le dépôt : `bash ps5/deploy.sh <ip-de-la-ps5>`.

Attends que ShadowMountPlus signale le titre prêt : **« rhythm-ps5 (dev) »** apparaît
dans les jeux (icône rose et blanche).

## 2. Lancer le récepteur de logs AVANT l'appli

Sur le PC, dans le dossier du dépôt :

```
python3 tools/logrecv.py <ip-de-la-ps5>
```

Il attend l'appli et réessaie en boucle. Au démarrage, l'appli **patiente 3 secondes**
qu'un PC se connecte avant d'initialiser la vidéo : on capte donc aussi un plantage
pendant l'init graphique. Les logs sont enregistrés dans `logs/<date>.log`.
L'appli affiche aussi une notification avec son IP et le port (9025).

## 3. Tester (étape 3 : interface façon lazer)

1. **Intro** : « bienvenue sur rhythm », triangles, icônes, grand triangle, flash blanc.
   N'importe quelle touche la passe.
2. **Premier lancement** (une seule fois) : l'écran « configuration initiale » télécharge
   le tutoriel et 20 maps de base. Tu peux continuer (Croix) pendant que ça télécharge.
3. **Menu** : Croix (ou clic sur le logo) ouvre la bande de boutons. Gauche/droite pour
   choisir, Croix pour valider, Rond pour revenir au logo seul.
4. **Sélection de map** : haut/bas = difficultés et maps, gauche/droite = map
   précédente/suivante, L1/R1 = saut de 8, L2/R2 = début/fin, **Carré = mods**,
   Triangle = au hasard, Options = réglages, Croix (ou clic sur le logo en bas à droite)
   = jouer. Dans les mods : **DT** (accéléré, même hauteur), NC, HT, EZ, NF, HR, HD, Auto.
5. **Chargement** : le logo et les infos de la map, puis la partie démarre seule.
6. **En jeu** : comme avant. **Options** = pause (Continuer / Recommencer / Quitter).
7. **Résultats** : l'anneau de précision se remplit, la lettre de rang apparaît.
   Carré = rejouer, Triangle = corriger le décalage (si proposé), Rond = retour.
8. **Options** (bouton du menu ou Options) : panneau à gauche, L1/R1 = sections.
   Essaie **Style de jeu** : « Épuré » (façon WhiteCat) ou « Triangles » (façon lazer).
9. **Télécharger** (bouton du menu ou icône en haut à droite) : L1/R1 = tri,
   L2/R2 = page, Croix = télécharger.

Clavier/souris (PC) : flèches + Entrée/Échap, Z/X pour frapper, souris partout, molette,
F1 mods, F2 hasard, F3 options, Espace passer.

**Sons personnels** : dépose des fichiers au nom des sons d'un skin osu!
(`normal-hitnormal.wav`, `soft-hitclap.ogg`, `menuback.wav`, `click-short.wav`…) dans
`/data/rhythm-ps5/sons/` (FTP) : ils remplacent les sons synthétisés au prochain lancement.

## 4. Ce qu'il faut me renvoyer

1. Le fichier `logs/<date>.log` (le plus important).
2. Une **photo de l'écran** avec le panneau DEBUG visible (la ligne « DEBUG (ps5, …) »,
   AUDIO, MANETTE, et la liste des dossiers `/data`, `/mnt/usb0`… avec
   existe/lisible/inscriptible).
3. Ton ressenti, en quelques mots :
   - le son des frappes arrive-t-il **tout de suite** ou avec un retard perceptible ?
   - le carré qui clignote et le tic sont-ils **ensemble** ?
   - le mouvement est-il **fluide** (saccades ?) ;
   - la valeur du « décalage conseillé » après ~30 frappes.
4. Ton **firmware** et le chargeur utilisé (ShadowMountPlus version…).

## Si ça ne marche pas

| Symptôme | Ce que ça nous apprend / quoi m'envoyer |
|---|---|
| Le titre n'apparaît pas | problème d'installation/chargeur : vérifie le chemin `/data/homebrew/PPSA99731/eboot.bin`, envoie une capture du dossier FTP |
| Écran noir puis retour à l'accueil, rien dans logrecv | plantage avant le réseau : récupère le fichier `/download0/ps5-opengl.log` si ton outil FTP voit `/user/download/PPSA99731/` (sinon dis-le-moi) |
| Logs reçus mais s'arrêtent à « init video » | souci EGL/ps5-opengl sur ton firmware : le log suffit |
| Bloc `!!! PLANTAGE` | copie-le tel quel : je le décode avec `tools/symbolize.py` et le fichier de symboles de la même build |
| Console qui redémarre | note à quel moment (avant/après l'affichage) |

Ferme complètement l'appli avant de réinstaller une nouvelle version.

## Pour les développeurs : build

```
bash ps5/build.sh            # 1080p 60 Hz ; REFRESH=120 pour du 120 Hz
cmake -S . -B build/pc -G Ninja && ninja -C build/pc && ./build/pc/rhythm-ps5   # version PC
```

Prérequis Ubuntu/WSL : `clang-18 lld-18 llvm-18 libclang-rt-18-dev make ninja-build
python3 curl wget unzip git` (+ `libsdl2-dev libgl-dev cmake` pour la version PC).
Le script télécharge et vérifie (SHA-256 / commits épinglés) le SDK payload, le
boilerplate et le SDK ps5-opengl 1.0.1 dans `.deps/`.
