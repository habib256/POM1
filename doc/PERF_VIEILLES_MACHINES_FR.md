# Optimiser POM1 en vitesse pour les vieilles architectures

*Rapport de profilage — 23 juillet 2026. Machine de mesure : Linux x86-64 moderne
(binaire Release `-O3` + LTO). Cible : les machines du plancher glibc 2.27
(l'AppImage bionic/18.04 validée sur Mint 19.x — Core 2 Duo, premiers i3,
Atom, iGPU Intel GMA/HD sous Mesa GL 3.2).*

> **Méthode.** Première passe au **gprof sans LTO** (type de build custom
> `Profile`, attribution de symboles fiable), `perf` étant bloqué par
> `perf_event_paranoid=4`. Scénarios headless **déterministes**
> (`--headless --dump-*-frame --dump-after-cycles N` : N cycles émulés exacts,
> plein régime, zéro pacing wall-clock). Deuxième passe **valgrind 3.22**
> (cachegrind géométrie Core 2 + callgrind) sur un build `-O2 -g` sans LTO ni
> `-pg` : résultats en **§8** — ils confirment les hotspots et **corrigent une
> hypothèse** (pas d'éviction L1, voir §8.2).

## 1. Mesures de référence (100 M cycles émulés, headless)

| Scénario | Wall | Débit | × temps réel (1,023 MHz) |
|---|---|---|---|
| Apple-1 + Integer BASIC (preset 3) | 1,95 s | 51,3 M cyc/s | ≈ 50× |
| GEN2 HGR (preset 11, beam-race actif) | 1,97 s | 50,8 M cyc/s | ≈ 50× |
| TMS9918 + CodeTank (preset 9) | 2,06 s | 48,5 M cyc/s | ≈ 47× |
| **A1-SID branché** (`--enable sid`) | **6,13 s** | **16,3 M cyc/s** | **≈ 16×** |

Autres chiffres : démarrage headless complet = **0,43 s wall / 0,64 s CPU**
(dont ~0,2–0,3 s de tables de filtre reSIDfp, voir R2) ; RSS ≈ **34 Mo**.

**Lecture pour une vieille machine.** Un Core 2 / Atom est ~8–15× plus lent en
mono-thread qu'ici. Le cœur d'émulation garde donc une marge de **3–6× le temps
réel** partout… **sauf SID : 16,3/12 ≈ 1,3× — à peine temps réel**, avant même
de payer l'UI, le GPU et l'audio callback. Le SID est LE chemin critique des
vieilles machines ; le reste est confortable.

## 2. Hotspots mesurés (gprof, attribution sans LTO)

### Preset GEN2 (émulation "générale")

| % self | Fonction | Appels | Note |
|---|---|---|---|
| 18,6 % | `Memory::memRead` | 85,7 M | ~3 lectures/instruction |
| 16,0 % | `Memory::advanceCycles` | 28,6 M | 1×/instruction — voir R3 |
| 9,0 % | `Memory::checkOutOfRangeAccess` | 71,4 M | sur CHAQUE accès — voir R4 |
| 7,7+7,7 % | `reSIDfp::Spline/OpAmp` | — | tables de filtre AU BOOT (R2) |
| 5,1 % | `PeripheralBus::tryReadSlow` | 14,3 M | lectures fenêtre GEN2 — voir R5 |
| 3,9 % | `M6502::executeOpcode` | 28,6 M | le CPU lui-même est déjà bon marché |
| 2,6 % | `CassetteDevice::advanceCycles` | 28,6 M | appel virtuel inconditionnel/instr. |

### `--enable sid` (le triplement du coût)

| % self | Fonction | Appels | Note |
|---|---|---|---|
| **32,2 %** | `reSIDfp::SID::clock` | **14,3 M** | **appelé par instruction** (~7 cycles/appel) |
| 15,9 % | `reSIDfp::Integrator6581::solve` | 100 M | le vrai DSP (1 par cycle SID) |
| 9,0 % | `reSIDfp::EnvelopeGenerator::clock` | 150 M | idem (3 voix) |
| 4,5 % | `pom1::SID::advanceCycles` | 14,3 M | mutex + ring par instruction |

Répartition affinée au callgrind (§8.3) : le DSP par-cycle inhérent à
libresidfp ≈ **55 %** du run, l'overhead d'appel (boucle de `SID::clock` par
micro-lot, wrapper `pom1::SID` + mutex par instruction) ≈ **15-20 %**, les
tables de boot ≈ 13 %, le cœur émulateur ≈ 12 %. *(Hypothèse initiale
corrigée : le `short staging[8192]` de 16 Ko est **réservé** sur la pile mais
seuls ~7 échantillons sont **écrits** — cachegrind mesure 0,0 % de D1 miss,
aucune éviction L1. Voir §8.2.)*

## 3. Recommandations, classées impact ÷ effort

### R1 — SID : clocker par lots, pas par instruction `[impact majeur · S/M]`

`Memory::advanceCycles` → `SID::advanceCycles(cycles)` à chaque instruction.
Correctif en trois pièces, sans perte d'exactitude :

1. **Accumuler** les cycles dans un compteur et ne appeler `chip->clock()`
   que (a) juste avant toute **écriture registre SID** (rattrapage exact — le
   timing des writes reste cycle-parfait) et (b) quand l'accumulateur dépasse
   ~512–1024 cycles. C'est le modèle classique resid ("lazy clocking").
2. **Réduire le staging** à la taille réellement produite par lot (≤ ~32
   shorts pour 1024 cycles à 44,1 kHz) — ou le sortir de la pile (membre).
3. **Virer le mutex du chemin chaud** : `chipMutex` ne protège que contre
   `setChipModel` (UI, rarissime). Un `std::atomic<bool> swapPending` testé
   sans lock + rattrapage sous lock uniquement quand il est vrai suffit.

Gain attendu (recalibré par le callgrind §8.3) : l'overhead d'appel pèse
15-20 % du run SID → 6,1 s → ~4,8-5,2 s. Le DSP par-cycle restant est
**inhérent au modèle d'exactitude de libresidfp** ; pour aller plus loin sur
vieille machine, ajouter un **silence-skip** : quand les 3 enveloppes sont à
zéro et le volume à 0 depuis N cycles, sauter `clock()` et pousser des zéros
dans le ring (un SID branché mais muet — le cas le plus fréquent — redevient
alors quasi gratuit).

### R2 — libresidfp : construction paresseuse `[startup · S]`

`Memory` construit `pom1::SID` inconditionnellement (`Memory.cpp:98`), et
libresidfp bâtit les tables de filtre **des deux modèles** (6581 **et** 8580,
threads `FilterModelConfig*`) à **chaque lancement, même SID débranché** —
visible dans TOUS les profils (~10 % du run !). Sur un Atom mono-cœur, ces
threads se sérialisent : **plusieurs secondes de boot perdues**.
→ Ne construire le chip qu'au premier `setSIDEnabled(true)` (ou première
écriture `$C800`), et ne bâtir que le modèle sélectionné. Le WASM fait déjà du
séquentiel gated — le desktop mérite le lazy.

### R3 — `Memory::advanceCycles` : remplacer les divisions par des compteurs `[M]`

16 % self, 1 appel/instruction. Deux coûts distincts :

- Chemin GEN2 : `cyc/cpf`, `cyc0/lpc`, `cyc1/lpc`… — **4 à 6 divisions 64 bits
  par instruction**. Une div u64 coûte 30–90 cycles sur Core 2/Atom (pas de
  diviseur rapide). Les incréments étant minuscules (≤ 7 cycles), tenir des
  compteurs **incrémentaux** (cycles-dans-la-ligne, ligne-dans-la-frame, avec
  soustraction au rollover) supprime toutes les divisions.
- `cassetteDevice->advanceCycles(cycles)` : appel virtuel inconditionnel par
  instruction (2,6 %) même sans cassette — le mettre derrière un booléen
  `cassetteActive` comme les autres cartes.

### R4 — OOR check : court-circuit au site d'appel `[S]`

`checkOutOfRangeAccess` est **appelée** sur chaque lecture ET écriture (71 M
appels — 9 %) pour presque toujours conclure "pas OOR". Un booléen
`oorPossible` (= `presetRamKB < 64`, mis à jour au changement de preset) testé
inline **au site d'appel** dans `memRead`/`memWrite` fait tomber le coût à un
test-and-branch prédit. ~7–9 % du cœur récupérés gratuitement.

### R5 — PeripheralBus : fast-path lecture RAM `[S/M]`

14,3 M lectures passent par `tryReadSlow` dans le preset GEN2 : la fenêtre
framebuffer `$2000-$3FFF` est enregistrée au bus, donc chaque lecture RAM de
cette page prend le chemin lent (stable_sort/priorités) pour finalement lire
`mem[]`. Marquer dans `pageMask` les pages dont le **read est pur
passthrough** (comme `onWrite = {}` existe déjà côté écriture) rend ces
lectures aussi rapides qu'une page sans périphérique.

### R6 — Côté GPU / UI des vieux iGPU `[déjà bien + 2 réglages]`

Déjà bon : écran texte = **1 seul AddImage** d'un FB 280×192 (rebuild
seulement au changement de grille), scanlines en ~400 quads batchés,
`SnapshotPublisher` à pages sales (Wozmon idle = zéro copie), uploads texture
≈ 215 Ko/frame/carte (négligeable).

À savoir pour les vieux iGPU :
- Le **shader CRT** (opt-in, OFF par défaut — bon défaut) rend chaque
  framebuffer **à la résolution fenêtre** ; 4 fenêtres ouvertes en 1080p ≈
  4 passes plein écran avec bicubique 16 taps → un GMA/HD2000 décrochera.
  Réglage simple si besoin : plafonner la résolution interne du pass CRT
  (p.ex. 2× le FB source) quand `GL_RENDERER` matche un iGPU ancien, ou
  exposer un "CRT qualité basse" (sampling bilinéaire, pas de bicubique).
- La **persistance** force un `process()` par frame même écran statique
  (ping-pong). CRT OFF, ce coût disparaît entièrement.
- Vsync ON (défaut GLFW) est l'ami des vieilles machines : pas de spin.

### R7 — Distribution `[déjà correct]`

Pas de `-march` : le binaire est **x86-64 baseline, sans AVX** — il tourne sur
Core 2 (2006). LTO + `-O3` déjà actifs. L'AppImage bionic garantit glibc ≥
2.27. Option future si besoin : un profil PGO (jouer les scénarios headless
ci-dessus comme charge d'entraînement), typiquement +5-10 % sur le cœur.

## 4. Ce qu'il ne faut PAS faire

- **Porter le décodage NTSC sur GPU** : déjà instruit dans `TODO.md` (§
  "défer jusqu'à un besoin perf concret") — ~3-4 M MAC/frame sur un buffer
  280×192, invisible au profil, et les vieilles machines ont un GPU encore
  plus faible que leur CPU. Le verdict tient toujours.
- **Toucher au cœur M6502** : `executeOpcode` = 3,9 % self. Il est déjà bon
  marché ; tout gain viendrait des à-côtés (R3/R4/R5), pas du dispatch.
- Multithreader davantage : les cibles anciennes sont mono/bi-cœur ; le
  design actuel (1 thread émulation + render) est le bon.

## 5. Ordre d'attaque suggéré

1. **R1 SID batch-clocking** — déverrouille le seul scénario sous-temps-réel.
2. **R2 SID lazy init** — secondes de boot sur Atom, une après-midi de travail.
3. **R4 OOR inline** — trivial, ~8 % du cœur.
4. **R3 advanceCycles** — divisions → compteurs ; attention au pin
   `gen2_beam_race_smoke` / `gen2_floatingbus_smoke` (byte-identique exigé).
5. **R5 bus read-passthrough** — pin `peripheral_bus_smoke` à étendre.

Chaque étape est verrouillable par les tests existants (`klaus`, `cpu_harte`,
`gfx_regress_*`, `gen2_*_smoke`) : aucun de ces changements n'a le droit de
modifier un seul octet de sortie.

## 6. Estimation après R1–R5 (vieille machine ÷12, recalibrée §8)

| Scénario | Aujourd'hui | Après |
|---|---|---|
| Cœur sans SID | ~4,2× temps réel | ~6–7× |
| SID branché, **muet** (silence-skip R1) | ~1,3× | ~5× |
| SID **en lecture** (musique active) | ~1,3× (limite !) | ~1,7–2× |
| Boot | 4–6 s | ~1–2 s |

Le SID en lecture active reste le scénario serré : son coût est le DSP
cycle-exact de libresidfp lui-même (§8.3). Si un jour ça ne suffit pas, la
seule marche supplémentaire est une option de qualité audio (chip alternatif
type reSID classique ou décimation) — à ne considérer que sur plainte réelle.

## 7. Commandes valgrind (exécutées le 23 juillet — résultats en §8)

À rejouer telles quelles **sur la 18.04** pour valider avec son cache/horloge :

```bash
sudo apt install valgrind

# Profil d'appels (équivalent gprof, plus précis, sans rebuild) :
valgrind --tool=callgrind --callgrind-out-file=cg.out \
  ./build/POM1 --headless --enable sid \
  --dump-gen2-frame /tmp/f.png --dump-after-cycles 20000000
callgrind_annotate cg.out | head -40        # (ou kcachegrind cg.out)

# Simulation de cache — LE test pertinent pour Core 2/Atom (L1 32K, L2 partagé) :
valgrind --tool=cachegrind --cache-sim=yes \
  --I1=32768,8,64 --D1=32768,8,64 --LL=2097152,8,64 \
  ./build/POM1 --headless --enable sid \
  --dump-gen2-frame /tmp/f.png --dump-after-cycles 20000000
cg_annotate cachegrind.out.* | head -40
# → résultat mesuré (§8.2) : D1 0,0 % — PAS d'éviction L1, hypothèse infirmée.

# Pics mémoire (RSS 34 Mo headless ; le rewind 128 Mo est le vrai budget) :
valgrind --tool=massif ./build/POM1 --headless --dump-after-cycles 5000000 \
  --dump-gen2-frame /tmp/f.png && ms_print massif.out.*
```

Attention : sous valgrind tout est 20–100× plus lent — réduire
`--dump-after-cycles` à ~20 M et ignorer le wall-clock, seuls les ratios
comptent.

## 8. Mesures valgrind (valgrind 3.22, 20 M cycles, géométrie Core 2)

Build `-O2 -g` sans LTO ni `-pg` (compteurs cache non faussés).

### 8.1 POM1 est compute-bound, pas memory-bound

| Scénario | Instructions (Ir) | Ir / cycle émulé | I1 miss | D1 miss | LL miss |
|---|---|---|---|---|---|
| GEN2 (preset 11) | 5,66 G | ~283 | 0,00 % | **0,1 %** | 0,0 % |
| `--enable sid` | 16,51 G | ~825 | 0,00 % | **0,0 %** | 0,0 % |

Avec la géométrie d'un Core 2 (L1 32 K/8 voies, LL 2 M), **tout tient en
cache** : pas de falaise mémoire à craindre sur les vieilles machines, la
vitesse scale linéairement avec l'IPC/fréquence mono-thread. C'est la
meilleure nouvelle du rapport — et elle valide de concentrer l'effort sur la
**réduction du nombre d'instructions** (R1–R5), pas sur la localité.

### 8.2 Hypothèse infirmée : pas d'éviction L1 par le staging SID

Le `short staging[8192]` (16 Ko) est **réservé** à chaque appel (le pointeur
de pile bouge, gratuit) mais seuls ~7 échantillons y sont **écrits** = 1 ligne
de cache touchée. D1 miss mesuré : 0,0 %. La recommandation R1 reste valable
mais pour l'**overhead d'appel**, pas pour le cache.

### 8.3 Répartition callgrind du run SID (16,5 G Ir = 100 %)

| Part | Quoi | Détail |
|---|---|---|
| ~55 % | **DSP libresidfp par cycle** (inhérent) | `WaveformGenerator` 11,6 % + `Integrator6581::solve` 11,4+2,7 % + `EnvelopeGenerator::clock` 11,3 % + `Filter*` 7,3 % + `ExternalFilter` 1,9 % + `Voice` 1,8 % + boucle `SID::clock` 3,8 % + accès tables 6,4 % |
| ~13 % | **Tables de filtre au boot** (R2) | `OpAmp::solve` 8,0 % + `Spline::evaluate` 5,4 % — payées à CHAQUE lancement, SID branché ou non |
| ~12 % | **Cœur émulateur** | `memRead` 4,1 % + `checkOutOfRangeAccess` 2,3 % + `advanceCycles` 2,1 % + `executeOpcode` 1,9 % + `tryReadSlow` 1,6 % |

Dans le run GEN2 (sans SID), les tables de boot pèsent même **~39 % des
5,66 G Ir** (OpAmp 23,4 % + Spline 15,8 %) — sur 20 M cycles, le boot domine.
Autre confirmation : `checkOutOfRangeAccess` fait ~7 écritures pile par appel
(sauvegarde de registres = pur overhead d'appel) → l'inline de R4 les
supprime intégralement.

## 9. Phase 2 — au-delà de R1-R7 (restructurations)

R1-R5 grattent des pourcentages *sans changer le modèle d'exécution*. Le
levier suivant est structurel. Point de départ chiffré : en régime établi
(§8, boot exclu), **~170 Ir/cycle émulé**, dont ~25-40 seulement de vrai
travail 6502 (`executeOpcode` + adressage + ALU ≈ 15 %). Le reste est de la
comptabilité **appelée à chaque instruction**. Trois chantiers, par ROI :

### P2-A — SID sur son propre thread `[M · le plus rentable]`

Le scénario serré (§6, SID en lecture ~1,7×) a une issue parfaite sur les
cibles réelles : un Core 2 **Duo** a un 2ᵉ cœur inoccupé pendant l'émulation.
Découpler le clocking SID du thread émulation : le thread émulation pousse un
**journal d'écritures registre horodatées en cycles** (exactement le modèle du
soft-switch journal GEN2 déjà dans `Memory::advanceCycles`), le thread SID
consomme le journal, clocke `libresidfp` par gros lots et remplit le ring
audio existant (déjà SPSC). Le DSP (55 % du run) disparaît du chemin critique
→ **SID actif ≈ gratuit pour l'émulation** sur tout dual-core. Précaution :
lecture de registre SID (rare, POT/OSC3) = point de synchronisation.

### P2-B — Dispatch mémoire par table de pages `[M]`

`memRead` (13 % Ir) re-teste à CHAQUE accès : alias PIA `$D0xx`, write-protect
ROM, OOR, `pageMask` du bus, sniffer cassette. Classique et prouvé : deux
tables de 256 handlers (`readPage[256]`, `writePage[256]`) reconstruites au
(dé)branchement de carte — le cas RAM pure devient `mem[addr]` après UN test
indexé, tous les cas spéciaux vivent dans leurs handlers. Absorbe R4 et R5 au
passage. Gain estimé sur le cœur : 1,3-1,5×. Pins : `klaus`, `cpu_harte`,
`peripheral_bus_smoke`, byte-identique exigé.

### P2-C — `advanceCycles` événementiel `[M/L]`

Aujourd'hui chaque instruction avance cassette + scanner GEN2 + N cartes
(16 % Ir + les virtuels). Modèle "next event" : calculer le **prochain cycle
intéressant** (rollover de frame GEN2, latch de scanline si le programme
écrit le framebuffer, échéance cassette, timer VIA…) et laisser le CPU
tourner en boucle serrée jusqu'à lui ; les périphériques se rattrapent
paresseusement depuis le compteur de cycles global (le scanner GEN2 est déjà
une fonction pure du cycle — il s'y prête). C'est le plus gros multiplicateur
du cœur mais le plus délicat : les pins beam-race (`gen2_beam_race_smoke`,
`gen2_floatingbus_smoke`, `gen2_horizontal_split_smoke`) sont le harnais.

### P2-D — UI adaptative pour vieux iGPU `[S]` — **IMPLÉMENTÉ (23 juil. 2026)**

L'émulation vit sur son thread ; l'UI n'a pas besoin de 60 Hz quand rien ne
change. Implémentation livrée : plein régime (vsync) pendant 10 s au boot,
2 s après tout événement d'entrée (callbacks GLFW → horodatage atomique,
événements pollés toutes les ~10 ms même en idle → latence de réveil d'un
tick), et tant que `MainWindow::wantsContinuousRender()` est vrai (sortie
Apple-1 en attente, fenêtre carte ouverte + CPU actif, shader CRT
(persistance), message status, compte-à-rebours de plug différé) ; sinon
**plancher ~5 Hz** — toute animation ratée par l'heuristique dégrade à 5 fps,
ne gèle jamais. Un callback WindowRefresh force le re-rendu sur expose (X11
sans compositeur). Toggle "Adaptive UI refresh" dans Display Settings,
persisté `ini/ui.settings` (`idle_throttle`), desktop seulement (le WASM
garde le rAF du navigateur). **Mesuré : Wozmon idle 30 s = 660 frames avec
vs 1500 sans (×10 moins de rendus en régime idle).** Reste ouvert du P2-D
d'origine : update de texture des cartes seulement si le framebuffer a
changé (la TMS copie 288×216 chaque frame même statique).

### P2-E — PGO au packaging `[S]`

Entraîner avec les scénarios headless du §1 (`-fprofile-generate` →
`-fprofile-use` dans le job release). Typiquement +5-15 % sur un
interpréteur, zéro risque fonctionnel, s'ajoute à tout le reste.

### À ne PAS faire (phase 2 incluse)

- **JIT/dynarec 6502** : pour une cible à 1 MHz déjà 50× temps réel, tout
  risque, zéro besoin.
- **Multithreader le cœur CPU+bus** : le 6502 est séquentiel, les cibles ont
  2 cœurs — P2-A les emploie mieux.
- **GPU NTSC** : toujours non (§4).

### Estimation cumulée (vieille machine ÷12, dual-core)

| | Aujourd'hui | R1-R5 | + Phase 2 |
|---|---|---|---|
| Cœur sans SID | ~4,2× | ~6-7× | **~12-15×** |
| SID en lecture | ~1,3× | ~1,7-2× | **~5× (P2-A : DSP sur cœur 2)** |
| UI idle (Wozmon) | 60 renders/s | — | ~2 renders/s (P2-D) |
