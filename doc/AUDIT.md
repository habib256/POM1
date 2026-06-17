# Rapport d'audit POM1 — passe 2

> **Statut (mis à jour 2026-06-17) — document historique.** Les **2 trouvailles « Moyenne »** (sous-comptage de cycles `JSR`/`RTS`) ont depuis été **corrigées** et pinnées par `cpu_harte_smoke` (oracle cycle-exact Tom Harte — cf. `CHANGELOG.md`). Les **2 « Faibles »** restent ouvertes, suivies dans `TODO.md` › *Durcissement désérialisation*. Conservé tel quel pour la traçabilité.

> Audit automatisé du code C++ de `src/` (42 fichiers `.cpp`, hors `third_party`), 2e passage.
> 15 grappes d'audit en parallèle → vérification adversariale → synthèse, puis **re-vérification manuelle** des trouvailles confirmées dans le code réel.
> Brut : **52 trouvailles → 3 confirmées par le workflow** (49 rejetées). La re-vérification manuelle confirme les 3 **et ajoute 1** (RTS, raté par le workflow). Généré le 2026-05-29.

## Résumé exécutif

Le code est globalement sain : **0 critique, 0 élevée, 1 moyenne, 3 faibles** (la moyenne et l'une des faibles forment une seule classe : sous-comptage de cycles sur les instructions à pile). Les correctifs de la passe 1 (CFFA1/Drive1541/CassetteDevice/A1IO_RTC/WiFiModem/main_imgui) ne sont **pas re-signalés** → régression non observée. Les changements récents (`irqStrapped`, dots par défaut) ne remontent aucun bug. Le sous-système le plus exposé est le **CPU 6502** : `JSR` **et** `RTS` comptent 4 cycles au lieu de 6, ce qui fausse le timing cycle-accurate (stall refresh DRAM, tempo SID, base de temps cassette). Les deux autres points relèvent de la robustesse d'entrées non fiables (snapshot PR-40, argument CLI).

## Tableau récapitulatif

| Sévérité | Sous-système | Fichier:ligne | Titre |
|----------|--------------|---------------|-------|
| Moyenne | CPU 6502 | `src/M6502.cpp:692-698` | `JSR` sous-compte les cycles (4 au lieu de 6) |
| Moyenne | CPU 6502 | `src/M6502.cpp:685-690` | `RTS` sous-compte les cycles (4 au lieu de 6) — *ajout re-vérif, raté par le workflow* |
| Faible | IO divers (PR40) | `src/PR40Printer.cpp:136` | `fifoLevel` désérialisé sans bornage → lecture OOB sur `fifo[40]` |
| Faible | CLI & main | `src/CliDispatcher.cpp:85-94` | Overflow d'entier signé dans `parseIntPositive` (cast `long`→`int`) |

---

## Sévérité Moyenne — classe « sous-comptage de cycles (instructions à pile) »

Modèle de cycles : `executeOpcode()` initialise `cycles = 1` (fetch opcode), puis appelle `addrMode()` puis `operation()`. Le total doit égaler le compte 6502 réel (commentaire `M6502.cpp:1151`).

### 1. `JSR` sous-compte les cycles
- **Fichier:ligne** : `src/M6502.cpp:692-698` — opcode `0x20 = {JSR, nullptr}` (single-function).
- **Catégorie** : logic / emulation-accuracy
- **Description** : `JSR()` ajoute `cycles += 3`. Total = base 1 + 3 = **4 cycles**. Le `JSR` réel prend **6 cycles**. Sous-compte de 2.
- **Preuve** :
```cpp
void M6502::JSR(void) {
    uint8_t lo = memory->memRead(programCounter++);
    pushProgramCounter();
    programCounter = lo + (memory->memRead(programCounter) << 8);
    cycles += 3;            // → 1 (base) + 3 = 4, devrait être 6
}
```
- **Correctif recommandé** : `cycles += 5;` (2 fetch opérande + 3 push/saut), total 6.

### 2. `RTS` sous-compte les cycles *(ajout re-vérification)*
- **Fichier:ligne** : `src/M6502.cpp:685-690` — opcode `0x60 = {Imp, RTS}`.
- **Catégorie** : logic / emulation-accuracy
- **Description** : `Imp()` ajoute 1, `RTS()` ajoute 2. Total = 1 + 1 + 2 = **4 cycles**. Le `RTS` réel prend **6 cycles**. Sous-compte de 2. Le workflow ne l'a pas trouvé ; même cause que `JSR`.
- **Preuve** :
```cpp
void M6502::RTS(void) {
    popProgramCounter();
    programCounter++;
    cycles += 2;            // → 1 (base) + Imp(1) + 2 = 4, devrait être 6
}
```
- **Correctif recommandé** : `cycles += 4;` (total 6). **À étendre** : auditer `RTI` (0x40, attendu 6), `BRK` (0x00, attendu 7) et l'IRQ/NMI handler dans la même revue — ce sont les autres opcodes manipulant la pile.

---

## Sévérité Faible

### 3. `PR40Printer::deserialize` — `fifoLevel` non borné
- **Fichier:ligne** : `src/PR40Printer.cpp:136`
- **Catégorie** : memory-safety
- **Description** : `fifoLevel = r.readU8()` accepte 0–255 sans validation contre `kFifoCapacity = 40`. `flushLineLocked()` itère ensuite `for (i < fifoLevel) currentLine.push_back(fifo[i])` sur `uint8_t fifo[40]` → lecture hors-bornes (jusqu'à `fifo[254]`) sur snapshot corrompu/malveillant.
- **Preuve** :
```cpp
fifoLevel = r.readU8();                 // ligne 136 — pas de borne
// flushLineLocked(): for (int i = 0; i < fifoLevel; ++i) currentLine.push_back(fifo[i]);
// uint8_t fifo[kFifoCapacity];  // kFifoCapacity = 40
```
- **Correctif recommandé** : `fifoLevel = std::min<int>(r.readU8(), kFifoCapacity);`

### 4. Overflow d'entier signé dans `parseIntPositive`
- **Fichier:ligne** : `src/CliDispatcher.cpp:85-94`
- **Catégorie** : integer-overflow / undefined-behavior
- **Description** : `std::stol` renvoie un `long` ; seule la condition `n < 0` est testée. Pour une entrée `> INT_MAX` (ex. `2147483648`), le cast `static_cast<int>(n)` est un débordement signé (UB).
- **Preuve** :
```cpp
long n = std::stol(s, &idx, 10);
if (idx != s.size() || n < 0) return false;
out = static_cast<int>(n);              // ligne 91 — UB si n > INT_MAX
```
- **Correctif recommandé** : `if (idx != s.size() || n < 0 || n > INT_MAX) return false;`

---

## Recommandations transverses

- **Revue ciblée du comptage de cycles des instructions à pile/contrôle.** `JSR` et `RTS` sont tous deux faux (−2 cycles). Vérifier systématiquement `RTI`, `BRK`, et les handlers IRQ/NMI contre la spec 6502 ; croiser avec les opcodes déjà corrects (`JMP abs` = 1+Abs(2)+0 = 3 ✓, `PHA` = 1+Imp(1)+1 = 3 ✓, `PLA` = 1+Imp(1)+2 = 4 ✓). Ce timing alimente le stall DRAM, le tempo SID et la base de temps cassette — non couvert par le test Klaus (fonctionnel, pas cycle).
- **Validation systématique des entrées non fiables.** PR-40 et CLI répètent le motif « lire un champ externe sans le borner » déjà vu en passe 1 (CFFA1/Drive1541). Borner tout indice/taille issu de `deserialize()` et toute conversion de largeur d'entier dès la lecture.
