# Générer des images IA optimales pour la conversion HGR (Nano Banana → Pix)

Kit de prompt-engineering pour produire, avec un générateur d'images IA (calibré sur
**Nano Banana / Gemini**), des images qui se convertissent proprement en Apple II HGR via
l'importeur ii-pix-style de POM1 (`src/hgrpaint/HgrConvert.cpp`, éditeur HGR Paint → Import).

Le HGR affiche **280×192**, en **4:3** (pixels non carrés → l'image est écrasée
horizontalement), avec seulement **6 couleurs** issues de l'artifacting NTSC, plus un
dithering Floyd-Steinberg scoré en CAM16-UCS. « Faire une belle image » et « faire une image
qui survit au HGR » sont deux objectifs opposés : il faut une source proche d'une
**sérigraphie / affiche** (aplats francs, contours nets, palette déjà réduite).

## Les 6 couleurs HGR — valeurs hex EXACTES

Cibles des aplats purs, extraites de `src/hgrpaint/HgrConvert.cpp` (`kPalette[]`, palette MAME,
verbatim depuis `GraphicsCard.cpp`) :

| Couleur | Hex       | Remarque                              |
|---------|-----------|---------------------------------------|
| Noir    | `#000000` | —                                     |
| Blanc   | `#FFFFFF` | —                                     |
| Vert    | `#19D700` | vert vif, légèrement jaune            |
| Violet  | `#E628FF` | **violet-magenta VIF, PAS bleuté**    |
| Orange  | `#E66F00` | orange citrouille                     |
| Bleu    | `#1990FF` | bleu azur vif                         |

Le **violet est la couleur au fil du rasoir** : `#E628FF` est nettement magenta. Un violet
*bleuté* (ex. `#5040F0`) est plus proche du bleu `#1990FF` → Pix le bascule en bleu. Un violet
trop clair/rose (au-delà de `#E628FF` vers le blanc) dithère avec du blanc. Viser pile
`#E628FF`, côté magenta. De même, **il n'existe pas de bleu foncé/navy ni de vert forêt** en
HGR : les versions sombres remontent en clair. Demander directement les teintes vives.

## Les 7 règles d'or

Le **cœur couleur** (règles 1-5) — ce qui fait qu'une zone tombe propre ou se dithère :

1. **Fond noir pur** `#000000` — jamais de « presque-noir » (bleu nuit, dégradé, étoiles,
   brume) : tout ce qui est légèrement au-dessus du noir se dithère en mouchetis. Une
   silhouette « noire » doit être `#000000` *strict*, sans le moindre reflet clair (sinon
   elle prend une couleur — cas des arbres morts du marais qui ressortent verts).
2. **Exactement les 6 couleurs** — beige/sable/brun/gris/rose/jaune/navy sont des pièges : ils
   n'existent pas et se dithèrent (le sable d'un désert → rayures orange/noir).
3. **Style flat vector / poster** — aplats francs, contours noirs épais, aucun dégradé.
4. **Une seule nuance par zone** — pas de 2-tons (une mesa violet clair + violet foncé, ou une
   barbe orange avec mèches foncées, dithèrent en hachures). Définir les volumes par des
   **contours noirs**, pas par un ombrage plus foncé.
5. **Teintes calées sur les hex exacts** — le bon hex compte plus que « la bonne couleur en
   gros ». Cas d'école : un manteau vert *foncé/saturé* hachure entièrement (Pix dithère
   vert+noir pour approcher un vert absent) ; le même prompt en **vert `#19D700` plat** → aplat
   lisse. Une seule variable, résultat du tout au tout. Attention surtout au violet `#E628FF`
   (magenta, pas bleu) et aux vifs (bleu/vert — pas de versions sombres).

La **composition** (règles 6-7) — ce qui répartit le bruit dans l'image :

6. **Un gros sujet unique > une scène large détaillée.** Un sujet qui remplit le cadre (nain,
   phénix, monstre) se convertit toujours plus proprement qu'une scène à éléments fins (arbres
   fins, reflets). À 280 px, un trait d'1 px se casse en pointillés. Pour une scène : durcir
   les silhouettes en noir pur, **épaissir** les éléments, supprimer reflets et détails fins.
7. **Aplat = zéro tolérance, texture = le dither aide.** Sur une zone censée être lisse
   (manteau, ciel, logo) le moindre hachurage trahit. Sur un sujet **organique/texturé**
   (écailles, fourrure, pierre, feu) le dither résiduel *lit comme de la texture* et renforce
   le sujet — le « défaut » devient une feature. Choisir le sujet en conséquence.

## Méta-astuces

- **Sujet difficile** (humain, peau, matière organique) : la peau n'est aucune des 6 couleurs.
  Deux tactiques valides selon le rendu voulu :
  - **Cacher** la partie hors-palette derrière un élément palette-friendly. Ex. : un
    **astronaute** dont le visage est masqué par une visière bleue → aucune teinte chair.
  - **Styliser** : l'assumer dans une couleur de la palette. Ex. : un **détective au visage
    vert** `#19D700` → look noir monochrome, propre et volontaire.
- **Les « glow » magiques** (éclair, flamme, halo, boule, feux follets) sont des dégradés
  déguisés → exiger « hard-edged rays / sharp flat shapes, no glow », sinon ça dithère.
- **Réglages importeur POM1** : Colour noise ~0.10, Dither + Serpentine ON, Diffusion 0.03.
  Mais ~90 % de la qualité vient de la **source**, pas des sliders.
- **Aspect** : générer en **4:3 landscape** (Nano Banana sort souvent du 1:1 par défaut).
- **La description du sujet prime, le bloc de style complète.** Si la description définit déjà
  le fond/les couleurs, retirer les lignes du bloc qui feraient doublon ou contradiction.

## Bloc de style « or » (à coller à la fin de chaque prompt)

```
Retro screen-print / poster style, bold flat color areas, high contrast,
crisp hard edges, thick black outlines. No gradients, no shading, no
texture, no noise, no dithering. Pure flat black (#000000) background.
Use ONLY these 6 exact colors, each region one single uniform color:
black #000000, white #FFFFFF, green #19D700, violet #E628FF (a bright
magenta-purple, NOT blue), orange #E66F00, blue #1990FF. Never use tan,
beige, brown, gray, pink, yellow, navy or any in-between/darker shade —
if a color isn't in this list, use black. 4:3 aspect ratio, landscape.
```

## Template réutilisable (n'importe quel sujet)

```
A flat vector poster of {SUJET}, large and centered, filling the frame.
{Décrire chaque zone avec UNE des 6 couleurs : "bright green X, orange Y,
violet Z on flat black"}. {Si sujet organique/peau : le cacher derrière un
élément palette-friendly}.

[+ bloc de style « or » ci-dessus]
```

## Génération automatisée (Chrome + Gemini / Nano Banana)

Méthode pour générer les images directement depuis Claude Code en pilotant Gemini via
l'extension **Claude for Chrome** (skill `claude-in-chrome`), validée 2026-07-01.

**Pré-requis Chrome (une fois)** : dans `chrome://settings/downloads`, **décocher « Toujours
demander où enregistrer »**. Sinon chaque download ouvre un dialogue natif « Enregistrer sous »
que l'extension **ne peut pas piloter** (il est hors de la page web) → le batch se bloque.

**Boucle par image** :
1. `tabs_context_mcp` puis `navigate` vers `https://gemini.google.com/app` (session Google déjà
   connectée). Modèle « Pro » par défaut convient.
2. Cliquer le champ de saisie, `type` le prompt **en UNE seule ligne sans retour à la ligne**
   (un `\n` déclenche l'envoi prématuré), préfixé de `Generate a single 4:3 landscape image. `.
3. Cliquer le bouton d'envoi (flèche bleue), `wait` ~8 s, `screenshot` pour juger le rendu.
4. **Récupérer l'image — voie presse-papier (méthode recommandée, validée 2026-07-02)** :
   aucun réglage Chrome requis, aucun dialogue. Dans la page : canvas → `ClipboardItem` ;
   côté OS : lecture GTK (python3-gi, présent sur Ubuntu — pas besoin de xclip) :
   ```js
   const img = [...document.querySelectorAll('img')].find(i=>i.naturalWidth>200);
   const c = document.createElement('canvas'); c.width=img.naturalWidth; c.height=img.naturalHeight;
   c.getContext('2d').drawImage(img,0,0);
   const blob = await new Promise(res=>c.toBlob(res,'image/png'));
   await navigator.clipboard.write([new ClipboardItem({'image/png': blob})]);
   ```
   ```bash
   python3 -c "import gi; gi.require_version('Gtk','3.0'); from gi.repository import Gtk, Gdk; \
   pb = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD).wait_for_image(); \
   pb.savev('N0xx.png','png',[],[]); print(pb.get_width(), pb.get_height())"
   ```
   (Un POST vers un serveur localhost est bloqué par la CSP de Gemini — testé. Skill
   projet : `.claude/skills/nanobanana-hgr/`.)

   **Variante download historique** (nécessite le pré-requis Chrome ci-dessus) — le `fetch`
   du blob échoue (« Failed to fetch »), et le résultat de
   l'outil JS **bloque toute sortie base64**. La voie qui marche : dessiner l'`<img>` (déjà
   chargée, `naturalWidth` ≈ 1024) sur un `canvas`, puis déclencher un download programmé avec
   le bon nom :
   ```js
   const img = [...document.querySelectorAll('img')].find(i=>i.naturalWidth>200);
   const c = document.createElement('canvas'); c.width=img.naturalWidth; c.height=img.naturalHeight;
   c.getContext('2d').drawImage(img,0,0);
   const a = document.createElement('a');
   a.href = c.toDataURL('image/png'); a.download = 'N006.png';
   document.body.appendChild(a); a.click(); a.remove();
   ```
   (Le canvas n'est pas « tainted » car le blob est same-origin `gemini.google.com`.)
5. Avec le réglage Chrome ci-dessus, le fichier tombe dans `~/Téléchargements/N0xx.png` → le
   déplacer vers `SCOSWAMP.MORE/PNG/N0xx.png` (`mv`). Vérifier l'octet-count > 0.

**Gotchas** : le bouton « Télécharger » natif de Gemini n'affiche qu'un tooltip sous clic
synthétique (pas de download fiable) → toujours passer par le canvas + `<a download>`. Si Gemini
présente une vérification anti-bot, passer la main à l'utilisateur (ne pas la franchir).

## Génération automatisée (Pinokio + Forge Neo / Z-Image Turbo)

Alternative **100 % locale et scriptable** — c'est la voie retenue pour les **gros batches**
(validée 2026-07-01 sur les 49 nœuds du livre-jeu « Marais aux Scorpions », ~6 s/image).
Contrairement à Gemini, l'API renvoie l'image **en base64 dans la réponse JSON** → on l'écrit
direct sur disque, **aucun dialogue de download** à contourner.

**Démarrage** (skill `pinokio`, via `pterm`) :
1. Si le plan de contrôle est injoignable (`ETIMEDOUT` sur une IP réseau périmée dans
   `~/.pinokio/config.json`), c'est que **Pinokio n'est pas lancé** — démarrer l'app desktop
   (`/opt/Pinokio/pinokio`, détaché) et poller `http://127.0.0.1:42000/pinokio/path/pterm`
   jusqu'à réponse. Au lancement, Pinokio réécrit `access.host` avec l'IP LAN courante.
2. `pterm run "pinokio://<host>:42000/api/forge-neo.git" --default start.js` **sans `timeout`
   autour** (un `timeout` qui tue `pterm` tue le process enfant Forge). Poller
   `http://127.0.0.1:7860/` jusqu'à HTTP 200 (startup SD-WebUI ≈ 1-2 min).

**Réglages Z-Image Turbo qui marchent** (modèle distillé flow-matching) :
- **`cfg_scale` = 1.0 impératif** (1.5 → image noire NaN mono-couleur), scheduler **`simple`**,
  sampler **Euler**, **8 steps**, `width×height` 1024×768.
- Appel : `POST http://127.0.0.1:7860/sdapi/v1/txt2img` avec `{prompt, negative_prompt, steps,
  cfg_scale, width, height, sampler_name, scheduler, seed}` ; l'image est dans `images[0]`
  (base64) → `base64.b64decode(...split(",")[-1])` et écrire le `.png`.
- Vérifier que le checkpoint actif est `z_image_turbo_bf16` via `/sdapi/v1/options`
  (`sd_model_checkpoint`) ; sinon le poser via `POST /sdapi/v1/options`.

**Forces / faiblesses de Z-Image vs Nano Banana** (mêmes prompts HGR) :
- ✅ **Batch massif** : entièrement scriptable, local, gratuit — Nano Banana est ingérable en
   batch (blocage download natif de Chrome).
- ⚠️ **Palette moins docile** : malgré le negative prompt, Z-Image ajoute souvent un **fond
   coloré** (vert, **beige**) au lieu du noir pur, du **jaune** dans le feu, du **gris** sur la
   pierre. Sur ~49 images, ~5 avaient un fond clair/coloré à refaire.
- ✗ **Texte dans l'image illisible** — Z-Image ne sait pas écrire (« LE MARAIS AUX SCORPIONS »
   → « BItE BAtE »). Pour un écran-titre : générer le décor **sans texte** et incruster le
   titre soi-même (ou le laisser au mode texte de POM1).

| | Nano Banana (Gemini) | Z-Image Turbo (Forge) |
|---|---|---|
| Palette / fond noir | ★★★★★ | ★★★☆☆ |
| Texte dans l'image | correct | ✗ illisible |
| Automatisation batch | ✗ (download bloqué) | ★★★★★ |
| Coût / réseau | cloud, session Google | 100 % local (GPU NVIDIA) |

**Règle de choix** : **Nano Banana** pour la qualité fine, les titres, les pièces héro ;
**Z-Image** pour abattre un gros volume automatiquement (quitte à repasser les ratées).
Autres checkpoints Forge utiles repérés : `pixelArtDiffusionXL_spriteShaper` (pixel-art),
`flux1-dev`. Le negative prompt anti-hors-palette standard :
`gradient, soft shading, glow, 3d render, realistic, photo, gray, brown, beige, yellow, red,
pastel, colored background, colored wall, noise, blurry, text, watermark`.

## Prompts validés (calibrés sur les 4 styles, 2026-07-01)

**Jeu vidéo / rétro** (excellent) :
```
An 8-bit arcade hero: a knight in bright green armor holding a glowing orange
sword, facing a violet dragon with blue wings. The two figures large and
centered, filling the frame. Big bold orange title text across the top.
Nothing in the background — pure flat black.  [+ bloc « or »]
```

**Paysage** (le ciel est l'ennemi juré — dégradé qui remplit l'écran → forcer noir) :
```
A retro night desert in flat vector poster style: a large solid orange sun on
the horizon, a solid flat deep blue-violet mesa mountain, bright green cactus
silhouettes. The ground and sky are BOTH pure flat black (#000000) — no sand,
no beige. The cacti and mesa sit directly on black.  [+ bloc « or »]
```

**Portrait** (le boss final — la peau n'est aucune des 6 couleurs → la cacher) :
```
Bold flat vector portrait of an astronaut, white helmet with thick black
outline, visor a single solid flat blue shape with one flat green reflection,
orange suit collar. Face hidden behind the reflective visor. Helmet fills the
frame.  [+ bloc « or »]
```

**Logo / illustration** (terrain idéal, quasi sans-faute) :
```
A flat vector emblem logo: a bright green apple with a white bite mark and a
bright orange leaf, centered inside a thick violet (#E628FF magenta-purple,
not blue) ring, on pure flat black. Thick black outlines, each shape one
single uniform flat color.  [+ bloc « or »]
```

## Cas A/B de référence (le *pourquoi*, une variable changée)

Deux paires « avant → après » où **seule une variable a bougé** : elles isolent la cause et
valent mieux qu'un long discours pour comprendre les règles 5 et 6.

### A/B n°1 — les hex exacts (règle 5)

Même détective, même palette « en gros », seule la teinte du manteau change.

- **AVANT** — *« green trench coat »* (vert conceptuel, foncé/saturé) → tout le manteau
  **hachure** : Pix dithère vert+noir pour approcher un vert absent de la palette.
- **APRÈS** — *« bright green #19D700 trench coat, ONE single flat green, no darker folds »* →
  **aplat lisse**.

> Leçon : « la bonne couleur en gros » ne suffit pas — il faut **le hex exact** (`#19D700`), et
> une seule nuance. Une teinte proche mais absente se dithère systématiquement.

### A/B n°2 — la densité de détail (règle 6)

Même éléphant, même bleu `#1990FF` stylisé (peau grise → bleu), seuls les ornements changent.

- **AVANT** — coiffe indienne dentelée (paisley, perles, bijoux 1-2 px, touches de rose) →
  **champ de bruit multicolore** : détail sub-pixel cassé en pointillés + rose hors-palette.
- **APRÈS** — *« a SIMPLE bold headdress made of a FEW large flat shapes only — one orange
  forehead plate, two violet ear discs, a green band. No tiny beads, no paisley, no pink »* →
  **aplats nets**.

> Leçon : à 280 px, **peu de grandes formes** bat toujours **beaucoup de petits motifs**. La
> stylisation résout la *teinte* (gris→bleu), pas la *densité* — les deux sont indépendantes.
