# TMS LOGO — turtle interpreter

Apple 1 LOGO interpreter targeting the P-LAB TMS9918 Graphic Card.
Mode-2 bitmap turtle, REPL with line editor, error reporting, user
procedures with parameters and tail-recursive calls, IF / IFELSE / STOP
/ arithmetic, REPEAT (incl. FOREVER + ESC abort), banner / demo. Links
three modules together (TMS_Logo[_16k] + math + tms9918m2).

## Status

Two builds ship side-by-side:

| Build | Source | Linker config | Target |
|-------|--------|---------------|--------|
| **V1.8** (frozen, 8 KB) | `TMS_Logo.asm` | `apple1_logo.cfg` | stock 8 KB Apple-1 (cassette / Woz hex paste) |
| **V2.6** (active, 16 KB) | `TMS_Logo_16k.asm` | `apple1_logo_16k.cfg` (DRAM) / `apple1_logo_v2_codetank_bank.cfg` (CodeTank ROM) | 16 KB Apple-1 *or* CodeTank upper bank |

V1.8 is at the absolute byte limit of the 8 KB CODE/PROC areas — it
only receives bug fixes that don't grow the binary. Active development
happens on the 16 KB variant.

## Hardware

- Machine: Apple 1 (8 KB DRAM for V1.8, 16 KB DRAM **or** CodeTank ROM
  for V2.6).
- Cards: P-LAB TMS9918.
- Recommended POM1 preset: **8** — *P-LAB Apple-1 with TMS9918 (CodeTank
  daughterboard)* (`MainWindow_Presets.cpp`). Flip the CodeTank jumper to **Upper** and
  type `4000R` from Wozmon to launch the V2.6 interpreter.

## Build

```sh
make            # V1.8 (8 KB), writes ../../../software/tms9918/TMS_Logo.{bin,txt}
make v20        # V2.6 (16 KB), writes ../../../software/tms9918/TMS_Logo_16k.{bin,txt}
```

CodeTank ROM (bundles Galaga / Sokoban / Snake / Life lower bank +
TMS_LOGO V2.6 upper bank):

```sh
python3 tools/build_codetank_rom.py --layout=menu
# writes roms/codetank/galaga_sokoban_menu.rom (32 KB)
```

The shipped library image is `roms/codetank/Codetank_GAME1.rom` —
re-sync it from the menu build after touching anything that ends up in
the V2.6 ROM:

```sh
cp roms/codetank/galaga_sokoban_menu.rom  roms/codetank/Codetank_GAME1.rom
cp roms/codetank/galaga_sokoban_menu.txt  roms/codetank/Codetank_GAME1.txt
```

## Run

1. POM1 → Presets → **8** (TMS9918 + CodeTank).
2. **Cassette / DRAM build (V1.8 or V2.6 standalone)**: File → Load →
   `software/tms9918/TMS_Logo.txt` (or `TMS_Logo_16k.txt`), then `280R`
   from Wozmon.
3. **CodeTank ROM (V2.6)**: jumper Upper → `4000R`. Lower jumper boots
   the games menu instead.

## Language summary (V2.6)

### Turtle

`FD/FORWARD n`, `BK/BACK n`, `TR/RIGHT n`, `TL/LEFT n`, `PU/PENUP`,
`PD/PENDOWN`, `HOME`, `CS/CLEARSCREEN`, `SETXY x y`, `SETH n`.

### Console

`PRINT "WORD` (or `PRINT N`), `WAIT N`, `PAUSE N`, `BYE`, `HELP [N]`.

### Control flow

- `REPEAT N [ ... ]` and `REPEAT FOREVER [ ... ]` (ESC / Ctrl-G aborts
  cleanly, procs survive).
- `IF a OP b [ ... ]`, `IFELSE a OP b [yes] [no]`. `OP` is one of
  `< > = <= >= <>`. Encoded as a 3-bit "allowed signs" mask
  (NEG/ZERO/POS) and decided by a single 16-bit signed subtraction.
- `STOP` exits the current proc.

### Variables and procs

- `MAKE NAME N` then `:NAME` reads. Single-level arithmetic in arg
  position: `:size + 2`, `:a - 1`. `RANDOM N` for `0..N-1`.
- `TO NAME :p1 :p2 ... END`. **10 procs**, **224 B body**, 6-char
  identifiers. Tail recursion costs zero frames; non-tail recursion uses
  a 1 024 B / 16 frame control stack in PROCBSS.

### Dynamic-turtle sprites (V2.5+)

`SETSHAPE "NAME` swaps the 16×16 hardware sprite that represents the
turtle. Built-in shapes:

- Locomotion: `BIRD1`, `BIRD2`, `TURTL`, `BOAT`.
- Narrator emotes (used by `DEM2`, *CodeTank build only*): `NORMAL`,
  `SHADES`, `SAD`, `UPSET`, `SICK`, `SUPER`, `GRUMPY`, `HAPPY`,
  `PIRATE`, `SLEEP`, `PERV`, `ANGRY`.
- Sentinel: `ARROW` switches back to the bitmap-triangle turtle (the
  default — XOR-style overlay rotates at every angle).

The first non-`ARROW` `SETSHAPE` flips the VDP into 16×16-sprite mode
and erases any visible bitmap turtle so the two render paths don't
ghost each other.

### Single colour command (V2.6)

`SETPC N` is the **only** colour command. `N` ∈ 0..15 is the standard
TMS9918 palette (`1=black, 2/3=greens, 4/5=blues, 6=dark red, 7=cyan,
8/9=reds, 10/11=yellows, 12=dark green, 13=magenta, 14=gray,
15=white`). It drives every colourised surface simultaneously:

- **Trail**: `plot_set` OR-mode writes the colour cell with `pen_color`.
- **Bitmap arrow** (default `ARROW` shape): the OR-mode triangle draw
  paints its outline cells with `pen_color`. SETPC immediately erases
  and redraws the visible arrow so the new tint takes effect without a
  move.
- **Sprite-0** (any `SETSHAPE` other than `ARROW`): `draw_turtle`
  re-emits the sprite-0 attribute byte with the new tint.
- **Bitmap text** (`LABEL` and `LIST`/`EDIT`): `blit_glyph` writes the
  cells' colour table along with the pattern bytes.
- **`SAY` bubble** (CodeTank build): the speech-bubble outline + glyph
  cells are temporarily forced to **white** while the bubble renders so
  it stays readable regardless of the sprite tint. The pen_color is
  restored after each `SAY` so subsequent commands see the user value.

The old `SETTC` / `SETSC` commands are gone — there is one source of
truth.

### Bitmap text (V2.5+ / CodeTank build)

- `LABEL "TEXT` — print on the bitmap at the current turtle position
  (Apple-1 charmap glyphs, 8×8). Multi-word text until CR or `]`.
- `SAY "TEXT` — comic-book speech bubble (240×32 px outline at
  (8,80)-(247,112) with a tail pointing up to a sprite at ≈(128,64)).
  Up to 3 wrapped lines of 28 chars. Pauses proportionally to line
  count.
- `LIST [NAME]` — dump a proc body to the bitmap. `EDIT NAME` opens a
  fullscreen line editor (Ctrl-K up, Ctrl-J down, Ctrl-X save, Ctrl-Q
  quit).

### Built-in demos

- `DEMO` — bitmap turtle slideshow. Includes `STAR`, `SUN`, `ROSETTE`,
  `RANDOM`, `FLOWER` (proc demo), `SPIRAL91` (deep tail recursion),
  `HEXAGON`, `STAR7`, `BURST`, `PINWHL`, `KALEID`, `GARDEN`, `CIRCLES`,
  `RAYS`, `BIRDFLY` (figure-8 sprite flight). Resets every colour to
  white at the end.
- `DEM2` (CodeTank build) — narrator slideshow exercising every emote
  sprite + bitmap-text bubbles. The "ill" scene shows the sprite in
  green, "anger" in red. Bubble text stays white throughout for
  readability.

## Internals (V2.6 highlights)

### Bitmap arrow draw/erase = save/restore (option B)

V2.6 ditched the XOR-overlay arrow in favour of a save/restore scheme
that leaves the bitmap **bit-for-bit unchanged** after a full
draw → erase cycle:

1. `compute_turtle_verts` derives the 3 triangle vertices (sin/cos of
   heading × tip/back distances).
2. `arrow_save_bbox` snapshots a **4×4 cell (32×32 px) bounding box**
   anchored on `(tx_lo - 9, ty_lo - 9)` snapped down to the cell grid,
   clamped to the 256×192 screen. 16 cells × 8 pattern bytes = 128
   bytes saved into `arrow_bg_pat` (in PROCBSS — LBUF was full).
3. The 3 triangle lines are then OR-traced (`plot_mode = 0`), so trail
   bits along the arrow's outline are *added to* (not toggled by) the
   arrow, and the cells' colour table picks up `pen_color`.
4. `erase_turtle` writes the 128 saved bytes back verbatim (replay in
   the same row-major order — no per-cell address bookkeeping needed).

Why 4×4 and not 3×3: the tip can extend ±9 px from `(tx, ty)` in any
heading; with a 3×3 box anchored on the cell grid the ±9 reach can
escape the saved region by up to 1 cell when the turtle sits in the
second half of a cell, leaving a single stray pixel after each move.
4×4 (32 px) covers ±15 px from the box centre and clears the whole
range at every position.

Compared to XOR:

- ✅ Background **never** modified after erase. No XOR's transient
  trail-bit inversion while the arrow is visible.
- ✅ No vertex-doubling artifact (OR is idempotent, so the 3 shared
  endpoints of the line triplet don't cancel each other out).
- ✅ 360° smooth rotation kept (still uses `compute_turtle_verts`).
- ✅ `cmd_setpc` simplifies to `erase_turtle` + `draw_turtle` — restores
  the saved bg, then re-saves and OR-draws with the new pen_color.
- ⚠️ ~50 % more cycles per move than XOR (~12 000 cycles vs ~8 000).
  Still well under one Apple-1 frame at 1 MHz; the LOGO interpreter
  itself dominates anyway.

### Save/restore unified routine

`arrow_save_bbox` and `arrow_restore_bbox` share a single body
(`arrow_io_bbox`) selected by an `arrow_io_dir` flag (0 = save / 1 =
restore). The two entry points use the classic 6502 `LDA #imm /
.byte $2C / LDA #imm` BIT-skip trick to fall through into the shared
body with the right flag preloaded.

## Limites — keep these in mind before adding features

The 16 KB V2.6 build sits very close to its CODE cap, and the linker
config is partitioned into tight zones. Any new code must respect them:

| Zone | Range | Cap | What lives here |
|------|-------|-----|-----------------|
| ZP | `$0000-$003F` | 64 B | scalar ZP scratch (M6502 lib + math + tms9918m2 imports). Adding `.exportzp` slots is essentially full. |
| LBUF (`LINEBUF` segment) | `$0200-$027F` | **128 B** | line buffer + parser scratch + every `.res` slot in the main `.segment "LINEBUF"`. Currently ~99 B used. **Adding more than ~25 B of BSS here will overflow** — push new buffers to `PROCBSS` instead (e.g. `arrow_bg_pat` lives there). |
| CODE | `$0280-$2FFF` (DRAM) **or** `$4000-$7FFF` (CodeTank) | **11 392 B** (DRAM) / **16 384 B** (CodeTank) | program code + RO data + lookup tables + demo / help strings. CodeTank slot is currently at **~16 320 / 16 384 B (≈ 99.6 %)**. **Anything larger than ~60 B must be paid for by trimming demo / help strings or compacting code.** |
| PROC (`PROCBSS` segment) | `$3000-$3FFF` (DRAM) / `$E000-$EFFF` (CodeTank upper RAM bank) | **4 096 B** | control stack 1024 B + var_table 48 B + proc_table 2440 B + param slots ≈ 18 B + arrow background save buffer 128 B = ≈ 3 658 B used. ~430 B free. |

When the linker reports `Segment 'CODE' overflows memory area 'CODE' by
N bytes`, the only options are: (a) compress code, (b) trim demo /
help strings, (c) move data to PROCBSS, or (d) bump version + grow the
CodeTank slot if there's room in the bank layout (unlikely without
displacing a game).

The 8 KB V1.8 CODE area is even tighter (`$0280-$1FFF` ≈ 7 552 B) and
the lib `tms9918m2.asm` is shared, so **lib changes that grow plot_set
or line_xy break V1.8** — keep the lib changes off the hot path. V2.6
does its colour-only repaint locally (vertex-toggle in `paint_turtle_*`
was retired with option B; the lib stays pristine).

## Sources

- `TMS_Logo.asm` — V1.8, 8 KB, frozen.
- `TMS_Logo_16k.asm` — V2.6, active.
- `apple1_logo.cfg` — V1.8 cassette / DRAM linker config.
- `apple1_logo_16k.cfg` — V2.6 standalone DRAM config (CODE at
  `$0280-$2FFF`, PROCBSS at `$3000-$3FFF`).
- `apple1_logo_v2_codetank_bank.cfg` — V2.6 CodeTank ROM config (CODE
  at `$4000-$7FFF`, PROCBSS at `$E000-$EFFF`).
- `apple1_logo_codetank_bank.cfg` — legacy V1.8 CodeTank slot
  (alongside Tetris).
- `emit_TMS_Logo_txt.py` — V1.8 hex emit. The 16 KB build uses an
  inline Python snippet in the Makefile.
- Libs: `dev/lib/apple1/`, `dev/lib/m6502/`, `dev/lib/tms9918/`. The
  `math.asm` and `tms9918m2.asm` modules are linked separately.

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
