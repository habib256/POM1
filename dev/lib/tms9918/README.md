# lib/tms9918 — P-LAB TMS9918 Graphic Card driver

*[← POM1 documentation index](../../../doc/README.md)*

Equates + drivers for the P-LAB Apple-1 TMS9918 card. Two modes shipped,
mutually exclusive (you pick one per project — links the matching `.o`).

## Files

- **`tms9918.inc`** — `VDP_DATA = $CC00`, `VDP_CTRL = $CC01` + the
  silicon-strict `WRT_DATA_REG` / `WRT_DATA_VAL` macros (see below).
- **`tms9918m1.asm`** — Mode 1 (Graphics I, 32×24 cells of 8×8 px) driver.
  Mutualises the init + upload + name-table writes that 4+ games
  (TMS_Sokoban, TMS_Connect4, TMS_Snake, TMS_Galaga) currently re-derive.
- **`tms9918m2.asm`** — Mode 2 (bitmap, 256×192) driver. Used by TMS_Logo.

### Fonts (shared with the GEN2 HGR card — Axe 2 of the lib factoring)

The Beautiful Boot 8×8 font now comes from ONE master, `dev/lib/hgr/bbfont_cp437.inc`,
emitted per format by `tools/build_shared_font.py` (`--check` to verify no drift):

- **`bbfont_tms.inc`** — *generated.* Full ASCII (0x20-0x7F, 96 glyphs) TMS9918
  pattern table, **bit 7 = leftmost pixel** (the bit-reverse of the HGR
  `gen2_bbfont.inc`). Inline fragment with a `tms_bbfont:` label — `.include` it
  at the pattern-table location and index `(ch-0x20)*8`.
- **`font_hud8x8.inc`** — *generated.* The 37-glyph HUD subset (char codes
  56..92) Snake/Sokoban draw for score/title text, now the BB font (was a
  separate hand-tuned font). Same on-ROM order/codes, so the games are
  unchanged; rebuild them to refresh the shipped artifacts.
- **`font_quale.asm`** — Quale's display font (independent, not from the master).

## Mode 1 (`tms9918m1.asm`) — public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `init_vdp_g1`     | 8 registers + tail-call disable_sprites                  |
| `disable_sprites` | Y=`$D0` to sprite #0 → chip stops scanning sprites       |
| `clear_name_table`| zero the 768-byte name table at `$1800`                  |
| `vdp_set_write`   | prep VRAM auto-increment write at `vdp_lo:hi`            |
| `vdp_set_read`    | prep VRAM read at `vdp_lo:hi`                            |
| `vdp_upload_a`    | A = count, copy from `(vdp_src_lo:hi)` to `VDP_DATA`     |
| `name_at_rc`      | `(vdp_row, vdp_col)` → `vdp_lo:hi` (no write yet)        |
| `print_at_rc`     | A = char, write at `(vdp_row, vdp_col)` — full sequence  |

### Owned ZP slots (6 bytes)

`vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col`

Distinct from Mode 2's `pix_*` / `ln_*` slots so a project that
hypothetically links both `.o` files would not collide. In practice m1
and m2 are mutually exclusive (different display modes); a project
picks one.

### Caller imports

`tmp` (1 ZP byte) — used inside `name_at_rc` and `vdp_upload_a`. Comes
free if you `.include "lib/apple1/zp.inc"` once.

### Mode 1 memory map (fixed by the register table)

| VRAM range | Purpose | Notes |
|---|---|---|
| `$0000-$07FF` | Pattern table | 256 chars × 8 bytes |
| `$1800-$1AFF` | Name table | 32 × 24 = 768 bytes |
| `$1B00-$1B7F` | Sprite attribute | 32 entries × 4 bytes |
| `$2000-$201F` | Colour table | **One byte per group of 8 chars** — design tile families starting at char 0/8/16/24/… |
| `$3800-$3FFF` | Sprite pattern | unused if disable_sprites |

## Mode 2 (`tms9918m2.asm`) — public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `init_vdp_g2`     | 8 registers + linear name table + colour table           |
| `clear_bitmap`    | zero the 6144 B pattern table at `$0000`                 |
| `disable_sprites` | Y=`$D0` to sprite #0 → chip stops scanning sprites       |
| `vdp_set_write`   | prep VRAM auto-increment write at `pix_addr_lo:hi`       |
| `vdp_set_read`    | prep VRAM read at `pix_addr_lo:hi`                       |
| `calc_pix_addr`   | `(pix_x, pix_y)` → `pix_addr_lo:hi` (no mask)            |
| `plot_set`        | plot at `(pix_x, pix_y)`, OR or XOR per `plot_mode`      |
| `line_xy`         | Bresenham `(ln_x0,y0)→(ln_x1,y1)`, 16-bit signed err     |

### Owned ZP slots (16 bytes)

`pix_x, pix_y, pix_addr_lo, pix_addr_hi, pix_mask, pix_byte, ln_x0, ln_y0,
ln_x1, ln_y1, ln_dx, ln_dy, ln_sx, ln_sy, ln_err, ln_err_hi`

### Caller imports

`tmp`, `tmp2` (1 ZP byte each) and `plot_mode` (1 BSS byte: 0 = OR,
1 = XOR). See `dev/projects/tms9918_logo/TMS_Logo.asm` for the
caller-side declaration template.

## Use

Mode 1 (typical game):

```asm
.include "apple1.inc"
.include "zp.inc"
.include "tms9918.inc"

.import init_vdp_g1, clear_name_table, vdp_upload_a
.import vdp_set_write, print_at_rc
.importzp vdp_lo, vdp_hi, vdp_src_lo, vdp_src_hi, vdp_row, vdp_col

main:
        JSR init_vdp_g1
        JSR clear_name_table

        ; Upload a custom pattern at char 8 → VRAM $0040
        LDA #$00 / STA vdp_lo
        LDA #$40 / STA vdp_hi
        JSR vdp_set_write
        LDA #<my_pattern / STA vdp_src_lo
        LDA #>my_pattern / STA vdp_src_hi
        LDA #128            ; 16 chars × 8 bytes
        JSR vdp_upload_a

        ; Print char 8 at row 12, col 14
        LDA #12 / STA vdp_row
        LDA #14 / STA vdp_col
        LDA #8
        JSR print_at_rc
```

Mode 2: `.include "apple1.inc"` + `.include "tms9918.inc"` + `.include
"tms9918m2.asm"`, callers also export `tmp/tmp2/plot_mode` (see
`tms9918_logo/TMS_Logo.asm:78-79` for the canonical declaration).

In your project Makefile (Mode 1 example, multi-object link):

    LIB := -I ../../lib/apple1 -I ../../lib/tms9918
    OBJS := MyGame.o tms9918m1.o
    tms9918m1.o: ../../lib/tms9918/tms9918m1.asm
        ca65 $(LIB) $@:= -o $@ $<
    $(OUT)/MyGame.bin: $(OBJS)
        ld65 -C my_game.cfg $^ -o $@

## VBlank synchronisation macro (`WAIT_VBLANK`)

**P-LAB câble /INT → /IRQ** (trace vérifiée sur le vrai matériel par
Parmigiani), mais le soft Nippur72 ne s'en sert pas. Recommandation :
**synchroniser les frames par polling** plutôt que par IRQ — c'est plus
simple et indépendant du flag I. L'IRQ frame est bien disponible (R1 bit 5 +
`CLI` + handler au vecteur $FFFE), mais tous les jeux POM1 (Galaga, Sokoban,
Snake, Life, Rogue, …) suivent le pattern polling.

```asm
.include "tms9918.inc"

        ; … main game loop …
@frame:
        WAIT_VBLANK            ; spin sur bit 7 de $CC01 jusqu'à F=1
        JSR render_sprites     ; ~4 554c "gate 2c" de bande passante VRAM
        JSR update_logic       ; …puis logique pendant que le faisceau redescend
        JMP @frame
```

`WAIT_VBLANK` se déroule en 7 octets :

```asm
        BIT VDP_CTRL           ; drain stale F (clears bits 5/6/7)
@vbl_wait:
        BIT VDP_CTRL
        BPL @vbl_wait
```

Side effect : la lecture de `$CC01` efface aussi les bits 5 (collision) et 6
(5S overflow). Si ton code dépend de ces flags, lis-les **avant** d'appeler
`WAIT_VBLANK` (ou snapshot le status register dans une variable). Pour les
jeux qui ne pollent que F, le clobber 5/6 est sans conséquence.

L'IRQ frame est câblée par défaut (`irqStrapped=true`) ; `TMS9918::setIrqStrapped(false)`
modélise au besoin une carte hypothétique non câblée. Si tu écris du code
pour P-LAB stock, le plus simple reste de poller — pas de configuration
nécessaire. Détails complets dans [`dev/SILICONBUGS.md`](../../SILICONBUGS.md) Bug N°2.

## Mid-frame raster trap — 5th-sprite-overflow primitive (`tms9918_5strigger.asm`)

Le polling V-blank donne **un** point de synchronisation par frame (la
dernière ligne / fin de l'active display). Pour planifier un événement
**au milieu** de la frame — palette split, name-table swap, upload de
patterns supplémentaires pendant la moitié basse — on peut détourner le
flag bit 6 du status register (5S = "5th sprite overflow") en plaçant 5
sprites invisibles à la ligne où on veut piéger le faisceau.

C'est exactement la technique de Daniel Vik dans la démo MSX *Waves*,
adaptée au polling pur — le TMS9918 n'a pas d'interruption ligne (seulement
le /INT frame), donc le mid-frame se polle quel que soit le câblage de /INT
(voir Bug N°2 dans `SILICONBUGS.md`).

### Public symbols

| Symbol            | Description                                              |
|-------------------|----------------------------------------------------------|
| `arm_5s_trigger`  | A = scan line (1..192) → écrit 5 sprites invisibles à cette ligne. SAT[0..4] consommés, SAT[5].Y = $D0 terminator. |
| `wait_5s_trigger` | spin `BIT $CC01 / BVC` jusqu'à ce que 5S = 1. Coût ~6c/itération. Préserve A. |
| `WAIT_5S`         | macro inline équivalente à `wait_5s_trigger` (4 octets, pas de `JSR`). Dans `tms9918.inc`. |

### Usage canonique

```asm
        .import arm_5s_trigger, wait_5s_trigger
        .import disable_sprites          ; tms9918m1.asm — pour désarmer
.include "tms9918.inc"

@frame:
        WAIT_VBLANK                       ; clean les flags 5/6/7
        LDA #96                           ; mid-screen scan line
        JSR arm_5s_trigger                ; 5 sprites invisibles à Y=95
        JSR upload_top_palette            ; couleurs pour la moitié haute
        WAIT_5S                           ; (ou JSR wait_5s_trigger)
        JSR upload_bottom_palette         ; swap mid-frame
        JSR disable_sprites               ; désarme avant la prochaine frame
        JMP @frame
```

### Caveats — à lire une fois

1. **Toute lecture de `$CC01` efface les bits 5/6/7 ensemble**. N'intercale
   PAS de `WAIT_VBLANK` entre `arm_5s_trigger` et `WAIT_5S` — le
   polling V-blank consommerait bit 6 au passage.
2. Le flag 5S **se latch sur la première ligne** où le 5e sprite est
   trouvé. Les lignes suivantes avec encore 5+ sprites ne le re-lèvent
   pas. Pour piéger plusieurs lignes dans une même frame, désarmer +
   ré-armer entre les deux waits.
3. Le chip compte sur la coordonnée Y uniquement. La position X et le
   pattern n'entrent pas dans le compteur — `arm_5s_trigger` utilise
   early-clock + couleur 0 (transparent) pour rendre les sprites
   invisibles indépendamment du contenu de la pattern table.
4. Si le programme utilise déjà des sprites pour le gameplay,
   `arm_5s_trigger` écrase les SAT[0..5]. Soit sauvegarder/restaurer
   autour de l'appel, soit réserver les 5 premiers slots du SAT comme
   "trigger sprites" maintenus à Y=$D0 par défaut, bumpés à la vraie Y
   uniquement au moment d'armer.
5. Désarmer après usage : `disable_sprites` (lib mode 1 ou 2) écrit
   Y=$D0 à SAT[0], le chip arrête tout scan SAT à partir de cette
   entrée. Sans désarmement, le 5S re-déclenchera à chaque frame.

### Coût

- `arm_5s_trigger` : ~25 stores VDP avec pad12 entre chaque ≈ 600 cycles.
  À faire 1× par frame, négligeable.
- `WAIT_5S` : ~6c × (lignes restantes jusqu'au trigger). Worst case
  ~12 000c quand on déclenche peu après un V-blank (ligne 8). C'est
  une perte sèche de bande passante CPU pendant l'attente — utiliser
  des splits **bas** dans la frame quand possible.

## Silicon-strict timing macros (`WRT_DATA_REG`, `WRT_DATA_VAL`)

When POM1's Hardware menu → **Silicon Strict** is ON (default for every
preset except the Multiplexing Fantasy ones), the TMS9918 enforces real-
silicon access windows: VRAM writes happening less than ~8 cycles apart
in Mode I + sprites are dropped. Two helper macros in `tms9918.inc` add
the right NOP padding between consecutive `STA VDP_DATA`:

```asm
; A already loaded with the byte to push (typical loop body).
WRT_DATA_REG     ; expands to: STA VDP_DATA / NOP / NOP

; Or load-immediate then push.
WRT_DATA_VAL #$AA  ; expands to: LDA #$AA / STA VDP_DATA / NOP
```

Both leave ≥ 8 cycles between the previous `STA VDP_DATA` and the next,
which matches the worst-case window in Graphic I + sprites. Use them in
new code; for an existing project, the patching playbook
([`dev/SILICONBUGS.md`](../../SILICONBUGS.md) §17 Annexe E) covers
mechanical NOP insertion across all back-to-back VDP stores. Reference
implementation: `dev/projects/tms9918_galaga/TMS_Galaga.asm` carries
~219 NOPs across its sprite / HUD / title / help routines.

The macros only matter when the program writes back-to-back during
*active display* (R1 bit 6 = 1). VRAM uploads done with display blanked
(R1 bit 6 = 0) get the relaxed 2-cycle window — `init_vdp_g1` /
`init_vdp_g2` could opt to blank around uploads to skip the macros, but
none currently do.

### Shortcut: skip `pad12` in "Mode I + no sprites" hot loops

If a project disables sprites for the whole frame (e.g. by leaving
`init_vdp_g1`'s tail-call to `disable_sprites` in place and never
re-arming the SAT), the silicon-strict floor drops to **6 c** between
consecutive VDP writes (vs 7.5 c in Mode I + sprites, vs 12 c in the
hardened pad12 contract). A natural 6502 inner loop already clears 6 c
without any padding:

```asm
@cell:  LDA (src),Y           ; 5c
        STA VDP_DATA          ; 4c — write happens here
        INY                   ; 2c
        BNE @cell             ; 3c (taken)
;                STA→STA gap = INY + BNE + LDA = 2 + 3 + 5 = 10 c (3.3× floor)
```

Reference implementation: `dev/projects/tms9918_plasma/TMS_Plasma.asm`'s
`render_frame` and `upload_patterns` deliberately drop `JSR
tms9918_pad12` in the hot path, taking the demo from ~22 fps to ~60 fps
without dropping any writes. The lib's `init_vdp_g1` / `vdp_set_write`
keep their `pad12` calls because they wrap `STA VDP_CTRL` register
writes (different timing class) and run in contexts that can't make
the no-sprites assumption.

**When to use this shortcut**:
- Pure graphics-I demos with no sprite use (plasma, scrollers, tile
  animations).
- Save the macros for whatever sprite arming / SAT updates the
  project still does — those keep the original gating.

**When NOT to use it**:
- Any project that uses sprites mid-frame (Galaga, Snake, Sokoban,
  CodeTank menu).
- Mode II projects (timing class differs).
- If you're unsure: the universal `WRT_DATA_REG` / `WRT_DATA_VAL`
  pad12 macros are always safe.

## Migration path for existing Mode-1 games

`TMS_Sokoban`, `TMS_Connect4`, `TMS_Snake`, `TMS_Galaga` each carry
their own copy of `init_vdp` (~70 lines), `vdp_set_write` (~6 lines),
`upload_pattern` (~12 lines). One-by-one migration:

1. Add `.include "lib/apple1/zp.inc"` to fold `tmp` into the project.
2. Replace local `init_vdp` body with `.import init_vdp_g1` + `JSR
   init_vdp_g1`.
3. Replace local `vdp_set_write` / `upload_pattern` calls with the
   library equivalents.
4. Switch the Makefile to multi-object link including `tms9918m1.o`.
5. Rebuild, byte-compare against the previous `.bin` to confirm
   semantic equivalence (the .bin will likely shrink since the lib
   factors away duplicated code).

Each migration drops ~80 lines of boilerplate from a project.
