# Optimiser POM1 en vitesse pour les vieilles architectures

*Rapport de profilage — 23 juillet 2026. Machine de mesure : Linux x86-64 moderne
(binaire Release `-O3` + LTO). Cible : les machines du plancher glibc 2.27
(l'AppImage bionic/18.04 validée sur Mint 19.x — Core 2 Duo, premiers i3,
Atom, iGPU Intel GMA/HD sous Mesa GL 3.2).*

> **Méthode.** `valgrind` n'était pas installé sur la machine de dev (et `perf`
> est bloqué par `perf_event_paranoid=4`), le profilage a donc été fait avec un
> **build instrumenté gprof sans LTO** (type de build custom `Profile`,
> attribution de symboles fiable) sur des scénarios headless **déterministes**
> (`--headless --dump-*-frame --dump-after-cycles N` : N cycles émulés exacts,
> plein régime, zéro pacing wall-clock). Les commandes valgrind prêtes à
> l'emploi sont en §7 — à lancer telles quelles une fois `apt install valgrind`
> fait, idéalement **sur la machine 18.04 elle-même** (c'est son cache et son
> horloge qui comptent).

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

Le DSP incompressible (Integrator+Envelope+Filter) ≈ 30 % ; **tout le reste du
coût SID est de l'overhead d'appel** : `chip->clock()` par instruction,
`lock_guard(chipMutex)` par instruction (28 M locks/s), et un
`short staging[8192]` — **16 Ko de pile touchés à chaque instruction** pour
récolter ~7 échantillons, ce qui évince le L1-D à chaque passage (mortel sur
un Core 2 à 32 Ko de L1).

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

Gain attendu : le run SID passe de 6,1 s à ~2,5–3 s (le DSP devient le seul
coût) → **le SID redevient confortablement temps réel sur Core 2**.

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

## 6. Estimation après R1–R5 (vieille machine ÷12)

| Scénario | Aujourd'hui | Après |
|---|---|---|
| Cœur sans SID | ~4,2× temps réel | ~6–7× |
| Avec SID | ~1,3× (limite !) | ~3× |
| Boot | 4–6 s | ~1–2 s |

## 7. Le jour où valgrind est installé

```bash
sudo apt install valgrind   # sur la machine de dev ET sur la 18.04

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
# → vérifier le D1-miss-rate de SID::advanceCycles (le staging 16 Ko, §2)
#   avant/après R1 : c'est la preuve chiffrée de l'éviction L1.

# Pics mémoire (RSS 34 Mo headless ; le rewind 128 Mo est le vrai budget) :
valgrind --tool=massif ./build/POM1 --headless --dump-after-cycles 5000000 \
  --dump-gen2-frame /tmp/f.png && ms_print massif.out.*
```

Attention : sous valgrind tout est 20–100× plus lent — réduire
`--dump-after-cycles` à ~20 M et ignorer le wall-clock, seuls les ratios
comptent.
