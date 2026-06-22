# applesoft_tms9918 — Applesoft + P-LAB TMS9918 graphics (CodeTank cartridge)

**Applesoft Lite** (Microsoft 6502 floating-point BASIC) with the **same command
set and token layout** as the sibling
[`sketchs/gen2/applesoft_gen2/`](../../gen2/applesoft_gen2/) — the two BASIC
dialects are deliberately *raccord* (a program tokenizes identically) — but every
graphics statement drives the **P-LAB TMS9918 VDP** (`$CC00` data / `$CC01`
control) instead of Uncle Bernie's memory-mapped GEN2 card.

It ships as a **CodeTank ROM cartridge**: the interpreter runs in place from the
`$4000-$7FFF` ROM window (like Tetris / LOGO V2.6), so cold-start with **`4000R`**
from the WOZ Monitor. Because the interpreter is ROM-resident (Applesoft descends
from Apple II's ROM Applesoft, so its body is ROM-clean) **and the TMS9918 keeps
all pixels in its own external VRAM**, the **entire RAM below the cart is free for
the user's BASIC program**: HIMEM is pinned at `$4000` (the cart floor), so BASIC
owns `$0801-$3FFF` (~14 KB). The Bench backs this with a real 16 KB low-RAM machine
(`$0000-$3FFF` RAM + CodeTank ROM `$4000-$7FFF`) — no non-physical 64 KB view — so
it matches a buildable Apple-1 + TMS9918 + CodeTank. Runs on POM1 preset **1
"Apple-1 TMS9918 Development Bench"**; the DevBench flashes this image as a CodeTank
dev cartridge.

Base: [txgx42/applesoft-lite](https://github.com/txgx42/applesoft-lite) (see the
shared interpreter notes in `sketchs/apple1/applesoft_lite/`).

## Graphics commands

Same statement/function tokens as Applesoft GEN2, mapped onto TMS9918 display
modes:

| Command | TMS9918 mode | Effect |
|---|---|---|
| `TEXT` | Text Mode F1 | 40×24 mono text (thin C64 font uploaded to VRAM); the console screen |
| `GR` | Multicolor | 64×48 of 4×4 colour blocks (use x 0–63, y 0–47), cleared |
| `HGR` | Graphics II | 256×192 bitmap, cleared (1 foreground colour per 8-px row) |
| `COLOR= n` | Multicolor | set block colour 0–15 (TMS palette) |
| `PLOT x,y` | Multicolor | x 0–63, y 0–47 |
| `HLIN x1,x2 AT y` | Multicolor | horizontal run of blocks |
| `VLIN y1,y2 AT x` | Multicolor | vertical run of blocks |
| `HCOLOR= n` | Graphics II | set pen colour 0–15 (TMS palette; 0–7 reads Apple-like) |
| `HPLOT x,y` | Graphics II | x 0–255, y 0–191 |
| `HPLOT x1,y1 TO x2,y2 [TO …]` | Graphics II | line(s), 8-bit Bresenham |
| `SCRN(x,y)` | Multicolor | read back a lo-res block colour |
| `HOME` `HTAB c` `VTAB r` | Text | clear / position the TMS text cursor |
| `NORMAL` `INVERSE` `FLASH` | Text | video attribute for `PRINT`. INVERSE renders white-on-black via a second (EOR $FF) glyph set uploaded at pattern $0500 = name codes $A0–$FF. FLASH ⇒ inverse (steady): Text Mode F1 has no per-cell blink attribute and only one spare name-table bit |
| `VBL` | any | wait for vertical blank (polls the VDP status register) |

This interpreter shares the exact renumbered BASIC body of the GEN2 sibling, so
the math2026 additions are identical: `SIN COS TAN ATN`, `DEF FN … / FN`, and
`PRINT TAB(n)`. See [`sketchs/gen2/applesoft_gen2/README.md`](../../gen2/applesoft_gen2/README.md).

```basic
10 HGR : HCOLOR=3
20 HPLOT 0,0 TO 255,191
30 HPLOT 0,191 TO 255,0
40 GR : COLOR=13 : PLOT 20,20
50 HLIN 0,39 AT 10 : VLIN 0,47 AT 5
```

## Writing text on the TMS9918 screen

`PRINT` writes to the **TMS text screen** while in `TEXT` mode (the cursor +
auto-wrap + scroll are built in — no `POKE` recipe needed, unlike the GEN2
build). `APRINT` writes to the **Apple-1 terminal** (WOZ ECHO, `$D012`). The
prompt / `LIST` / errors / `INPUT` echo always stay on the Apple-1 terminal.

```basic
10 TEXT : HOME
20 VTAB 5 : HTAB 10 : PRINT "HELLO TMS9918"
```

Because the TMS9918 has no separate text page, `PRINT` only paints the screen in
`TEXT` mode; issued in a graphics mode it falls back to the Apple-1 terminal
(painting the text name table behind a live bitmap would corrupt the picture).

## Differences from the GEN2 build (TMS9918 hardware reality)

The command surface is identical, but the chip is not an Apple-II framebuffer:

- **No hi-res page 2.** The 16 KB VRAM holds exactly one 256×192 bitmap (6 KB
  pattern + 6 KB colour + name table), so `HGR2`/`GR2` **alias** `HGR`/`GR` and
  `SHOW` is a no-op. (`GR`'s multicolor buffer is small, but a second hi-res page
  simply does not fit.)
- **No mixed text+graphics.** TMS9918 modes are mutually exclusive, so
  `MIX`/`NOMIX` are no-ops.
- **Colours are the TMS palette** (0 = transparent/backdrop, 1 = black, 15 =
  white, …), not Apple's. `COLOR=`/`HCOLOR=` take 0–15 and `SCRN` reads them back
  faithfully.
- **Hi-res is 256×192** (`HPLOT` x 0–255), not 280×192; **lo-res is 64×48**
  blocks (`PLOT` x 0–63). `HLIN 0,39` / `VLIN …` still work Apple-style.
- All pixels live in the card's private VRAM (no memory-mapped framebuffer), so
  every plot is a read-modify-write through `$CC00/$CC01` with silicon-strict pad
  gaps — slower than GEN2, but correct under POM1's default Silicon-Strict mode.

## Build / memory map

`.sketch.json` drives the DevBench build (`applesoft_tms9918.cfg`,
`extraAsm: io.s`; `tmsgfx.inc`, `macros.s`, `zeropage.s` and the
`dev/lib/tms9918` font/equates are `.include`d).

```
$0000-$00FF  ZP (Applesoft + TMS plot scratch $06-$0C, $0300 state, CSW @ $031E)
$0200-$02FF  WOZ / Applesoft input buffer       $0400-$07FF  text-scroll scratch
$0800-$3FFF  USER BASIC program + vars + strings (HIMEM=$4000; real 16 KB low RAM)
$4000-$7FFF  interpreter image — CodeTank ROM, runs in place (4000R)
$CC00/$CC01  TMS9918 VDP data / control          $FF00-$FFFF  WOZ Monitor (ECHO)
```

VRAM (inside the card) per mode: **TEXT** font `$0100` + name `$0800`;
**GR** colour buffer `$0000` + name `$0800`; **HGR** pattern `$0000` + colour
`$2000` + name `$3800`.

Pinned by the `applesoft_tms9918_smoke` ctest (core + PRINT→TMS text +
HGR/HPLOT + GR/PLOT/HLIN/VLIN + HOME/HTAB/VTAB + SCRN, all inspected in VRAM
with the card plugged).

## Example programs

- `FeatureDemo.apf` — the same 12-option demo as the GEN2 sibling (trig Function
  tests, Lissajous, Mandelbrot, Hires, Video/TAB, …), adapted to this card: lo-res
  PLOT uses 0–63, HPLOT clamps x to 255, and the Video page's INVERSE shows
  white-on-black (FLASH falls back to inverse). The full 21-option jsbasic demo is
  trimmed to 12 to fit the 6 KB program/variable RAM. Pinned by
  `applesoft_gen2_smoke` (`featuredemo-tms`).
