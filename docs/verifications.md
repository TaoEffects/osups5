# Vérifications techniques (13 octobre 2026)

Réponses aux six « points à vérifier en premier » du brief, plus une découverte qui
change la façon de tester. Sources consultées le 13/10/2026 ; les dépôts cités ont
été clonés et lus, pas seulement survolés.

## ⚠️ Découverte importante : une appli OpenGL n'est pas un payload

Le brief prévoit d'« envoyer les payloads par le réseau ». **ps5-opengl ne le
permet pas** : son README et `native-app/README.md` disent explicitement qu'une
appli OpenGL doit être empaquetée en **titre natif (dossier)**, copiée dans
`/data/homebrew/<TITLE_ID>/` et lancée depuis l'écran d'accueil comme un jeu
installé, *« do not send it to an ELF loader »*.

Conséquences :
- l'appli se construit avec le [boilerplate natif](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (même auteur que ps5-opengl) : `eboot.bin` signé en FSELF + `sce_sys/` + `sce_module/libc.prx` ;
- côté console il faut, en plus du HEN, un serveur FTP et un chargeur de titres en
  dossier (le boilerplate est validé avec **ShadowMountPlus**) ;
- le boilerplate et ps5-opengl n'ont été validés sur console qu'en **firmwares 6.02 et 12.70**.
  Les autres firmwares compatibles Relapse sont à tester (c'est l'objet du premier test).

Tout cela est automatisé dans `ps5/build.sh` (voir `docs/test-ps5.md`).

## 1. Licences

| Composant | Licence | Rôle |
|---|---|---|
| McOsu (McKay42/McOsu) | GPL-3.0 (fichier LICENSE) | code de jeu osu! (C++) |
| McEngine (McKay42/McEngine) | MIT | moteur de McOsu |
| neosu → renommé **neomod** (github.com/kiwec/neosu → neomodnet/neomod) | GPL-3.0 | fork maintenu de McOsu |
| ps5-opengl (blackbearreloaded, forks OpenAGC) | GPL-3.0-or-later | OpenGL 4.6 / 3.3 sur PS5 |
| ps5-native-app-boilerplate | GPL-3.0-or-later | empaquetage en titre natif |
| ps5-payload-sdk | GPL-3.0+ (en-têtes FreeBSD sous BSD) | chaîne de compilation |
| SDL2 (si utilisé) | zlib | — |
| font8x8 (police de l'écran de debug) | domaine public | — |

**Conclusion : tout est compatible.** Le projet est publié sous **GPL-3.0-or-later**
(fichier `LICENSE`). Combiné à McOsu (GPL-3.0 « tout court »), l'ensemble distribué
sera sous GPL-3.0, ce qui reste conforme. Obligations : publier les sources complètes
(y compris les scripts de build, c'est fait), garder les mentions de copyright.
Points restant à contrôler au moment d'importer du code de McOsu/neomod : la licence
des **assets** (skins, sons, polices) de ces dépôts, qui peut différer du code. Pour
l'instant le projet n'embarque aucun asset : textures et sons sont générés par le code.

## 2. Audio : pas de BASS, et c'est faisable

- **McOsu/McEngine** : BASS sur PC (fermé, pas de version PS5) ; la version Switch
  (McOsu-NX) utilisait **SDL_mixer** (`MCENGINE_FEATURE_SDL_MIXER`).
- **neomod** : **SoLoud par défaut** (`--with-audio=soloud`, BASS optionnel). Bonne nouvelle.
- **PS5** : `sceAudioOut` est disponible (bouchons dans le SDK payload) : ports 48 kHz,
  blocs de 256 à 2048 trames, `sceAudioOutOutput` bloquant = l'horloge naturelle.
  `sceAudioOutGetLastOutputTime` existe aussi, utile plus tard pour mesurer la latence réelle.

Ce qui est fait à l'étape 1 : un mixeur maison minimal (`src/audio/mixer.*`) sur
`sceAudioOut` en blocs de **256 trames (5,3 ms)**, avec programmation des sons à la
trame près et une **horloge audio interpolée** (même principe que le
`PlaybackInterpolator` de McOsu/neomod). Le banc d'essai mesure la régularité du
thread audio et l'écart moyen de frappe pour régler le décalage.

Reste à faire (étape 2) : décodage MP3/OGG en flux (dr_mp3/minimp3, stb_vorbis — domaine
public/MIT) et étirement temporel pour DT/HT (SoundTouch LGPL ou signalsmith-stretch MIT),
ou bien reprendre le moteur SoLoud de neomod en lui donnant un backend `sceAudioOut`.

## 3. Regroupement des dessins (batching)

Fait dès l'étape 1 (`src/render/batch.*`) : tous les quads d'une image sont envoyés en
**un seul transfert**, un appel de dessin n'est émis qu'au changement de texture.
Mesure sur PC : ~1800 quads en **10 appels** (le texte alterne avec les cercles) ; le
test de charge (Triangle) ajoute 5000 sprites sans ajouter d'appel. Prochaine
amélioration : un atlas de textures pour descendre à 1–2 appels.
ps5-opengl regroupe lui-même les soumissions GPU (`PS5_DEFERRED_DRAW_BATCH`), mais
chaque appel `glDraw*` coûte du CPU : on reste donc économe.

## 4. Entrées

| Périphérique | API | État |
|---|---|---|
| Manette DualSense | `scePadOpen/ReadState` | ✅ branché (sticks, gâchettes analogiques, pavé tactile 2 doigts, barre lumineuse) |
| Clavier USB | `sceKeyboardOpen/ReadState` | ✅ branché (codes HID, comme SDL) |
| Souris USB | `libSceMouse` | ❌ **aucun bouchon dans le SDK public**. Les bouchons ne sont que des noms de fonctions : on peut en écrire un (`sceMouseInit/Open/Read`), reste à vérifier que le module se charge dans une appli native. Étape 3. |
| Tablette | — | Si elle se présente comme une souris, même chemin que la souris. Pas d'API HID brute publique repérée. |

Les structures Sony (état manette 120 octets, état clavier) sont reprises du fork SDL
de ps5-payload-dev, déjà éprouvé, et verrouillées par des `static_assert`.

## 5. Maps et connexion

- **API osu! v2** (doc officielle, lue le 13/10/2026) : `GET /beatmapsets/{id}/download`
  est marquée **« lazer »** : réservée au client officiel, inaccessible avec les jetons
  tiers. Seuls deux types d'autorisation existent : *Authorization Code* et
  *Client Credentials* (pas de « device flow »). Demander le mot de passe osu! est exclu.
- **Miroirs**, testés depuis ce serveur le 13/10/2026 :

| Miroir | Recherche | Téléchargement `.osz` |
|---|---|---|
| osu.direct | ✅ `/api/v2/search`, `/api/v2/s/{id}` | ✅ `/d/{id}` |
| catboy.best (Mino) | ⚠️ `/api/v2/*` → 403 (blocage IP de datacenter ou User-Agent ? à retester depuis chez toi) | ✅ `/d/{id}` |
| Nerinyan | ❌ hors service (erreur Cloudflare 1033) | ❌ |
| chimu.moe | ❌ ne répond plus | ❌ |
| beatconnect.io | site en ligne, API non testée | — |

  → prévoir **plusieurs miroirs configurables** avec repli automatique (neomod a un
  réglage `beatmap_mirror_override` dans le même esprit).
- **HTTPS sur la console** : libcurl + OpenSSL (ports PacBrew) avec l'enveloppe `fcntl`
  documentée par le boilerplate (`docs/CURL.md`), ou `sceHttp` pour des requêtes simples.
- **Connexion optionnelle par QR code** : l'*Authorization Code* exige une URL de retour
  fixe déclarée à l'avance. L'IP de la PS5 n'est pas fixe, donc il faudra une petite
  page relais hébergée (qui affiche un code à saisir, ou que la console interroge) et
  le secret client ne peut pas être caché dans un logiciel libre. **À décider plus tard**,
  ce n'est pas bloquant : la connexion est optionnelle et n'est pas nécessaire pour
  télécharger depuis les miroirs.
- **Aucun envoi de score** : rien dans ce projet ne parle aux serveurs osu!. Attention,
  neomod contient un module d'envoi de scores (`BanchoSubmitter`, pour serveurs privés) :
  à retirer si on reprend son code.

## 6. Mémoire et stockage

- Tas applicatif de ps5-opengl : **128 Mio par défaut** (malloc est redirigé). Il peut être
  agrandi en définissant le symbole `ps5_opengl_heap_size` (au-delà de 128 Mio, la mémoire
  vient de la « direct memory »). → décoder la musique **en flux** (une chanson de 3 min
  décodée entièrement en float stéréo = ~69 Mo).
- `/download0` : inscriptible et persistant (taille réglée par `downloadDataSize` dans
  `param.json`). Logs et config y vont.
- Clé USB / disque interne : **inconnu depuis le sandbox** d'une appli native (le
  boilerplate déconseille `/data` et `/user`). L'appli teste au démarrage `/data`,
  `/mnt/usb0`, `/mnt/usb1`, `/mnt/ext0`, `/usb0`… et affiche ce qui est lisible et
  inscriptible : **le premier test console répondra à la question**. Une élévation des
  droits existe (exemple Lapy du boilerplate) si nécessaire.

## Choix de la base (recommandation)

| | McOsu + McEngine | neomod |
|---|---|---|
| Fenêtre/entrées | Abstraction `Environment` fine, déjà portée sur Switch via SDL2 | **SDL3 ≥ 3.4 obligatoire**, partout ; aucun port SDL3 homebrew PS5 connu |
| Audio | BASS (PC) / SDL_mixer (Switch) | SoLoud ✅ |
| Dépendances | légères | curl, OpenSSL, libarchive, freetype, zstd, lzma, SoLoud, FFmpeg (option)… |
| Maintenance | peu active (dernier commit févr. 2026) | très active (commits cette semaine) |
| Téléchargement de maps | non | oui (serveurs privés/miroirs) |

**Recommandation pour l'étape 2** : garder la couche plateforme de ce dépôt (validée
pas à pas sur console) et y porter le **code de jeu de McOsu** via un `Environment`
McEngine qui s'appuie sur elle, en récupérant **ponctuellement** dans neomod ce qui
manque (moteur SoLoud, téléchargement depuis miroirs), puisque tout est GPL. Porter
neomod tel quel demanderait d'abord un SDL3 pour PS5, chantier à lui seul.
Cette décision t'appartient : on en reparle après le premier test console.

## Non vérifié

- Le portage osu! 3DS de Towny (@towny1_) : pas de dépôt trouvé pour l'instant.
- Les affirmations du brief sur Relapse (firmwares 7.00–13.60) : non revérifiées ici,
  sans incidence sur le code. Seul compte le fait que ShadowMountPlus (ou équivalent)
  fonctionne sur ton firmware.
