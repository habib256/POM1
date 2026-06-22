# applesoft_gen2 — Applesoft + Uncle Bernie GEN2 graphics

**Applesoft Lite** (Microsoft 6502 floating-point BASIC) turned into the BASIC
for Uncle Bernie's GEN2 colour card: CFFA1 disk I/O removed, the GEN2 TXT / LGR /
HGR primitives added as BASIC statements, and **`PRINT` retargeted to the GEN2
screen**. Open `applesoft-gen2.s` in *DevBench → POM1 Bench* (GEN2 profile) and
**Run**: it cold-starts the interpreter at `$6000`. You type BASIC on the Apple-1
terminal (prompt, `LIST`, errors and `INPUT` echo stay there); program output and
graphics appear on the GEN2 card.

Base: [txgx42/applesoft-lite](https://github.com/txgx42/applesoft-lite) — see the
sibling [`sketchs/apple1/applesoft_lite/`](../../apple1/applesoft_lite/).

## Output model — PRINT vs APRINT

| Statement | Goes to |
|---|---|
| `PRINT …` | the **GEN2** text page (`$0400`), via a cursor with CR / wrap / scroll |
| `APRINT …` | the **Apple-1** terminal (WOZ ECHO `$D012`) |

Both take the full Applesoft print list (`;` `,` `SPC(` strings, numbers). The
output vector is the Apple II `CSW` idiom: it defaults to the Apple-1 ECHO, the
`PRINT` wrapper points it at the GEN2 console for its duration, `APRINT` forces it
back. To see `PRINT` output you must be in `TEXT` (or `MIX`) mode.

## Command set

| Command | Mode | Effect |
|---|---|---|
| `TEXT` | — | GEN2 text mode, page 1 |
| `GR` / `GR2` | lo-res | 40×48 colour, page 1 / page 2, cleared |
| `HGR` / `HGR2` | hi-res | 280×192, page 1 (`$2000`) / page 2 (`$4000`), cleared; default `HCOLOR`=white |
| `MIX` / `NOMIX` | — | mixed graphics+text / full-screen graphics |
| `SHOW n` | — | display page n (1\|2) **and draw on the hidden page** (double buffering) |
| `VBL` | — | wait for vertical blank (use in GR/HGR; needs the card) |
| `COLOR= n` | lo-res | colour 0–15 |
| `PLOT x,y` | lo-res | x 0–39, y 0–47 |
| `HLIN x1,x2 AT y` | lo-res | horizontal line |
| `VLIN y1,y2 AT x` | lo-res | vertical line |
| `HCOLOR= n` | hi-res | 0/4 black, 1 green, 2 violet, 3/7 white, 5 orange, 6 blue |
| `HPLOT x,y [TO x2,y2 …]` | hi-res | point / Bresenham line(s) |
| `HOME` | text | clear GEN2 text page + cursor home |
| `HTAB c` / `VTAB r` | text | cursor column (1–40) / row (1–24) |
| `SCRN(x,y)` | lo-res | **function** — read the colour at (x,y), 0–15 |

```basic
10 HGR : HCOLOR=3
20 HPLOT 0,0 TO 279,191 : HPLOT 0,191 TO 279,0
30 TEXT : HOME
40 VTAB 12 : HTAB 14 : PRINT "HELLO GEN2"
50 GR : COLOR=13 : PLOT 20,20 : HLIN 0,39 AT 0 : VLIN 0,47 AT 0
60 PRINT "PIXEL 5,5 = "; SCRN(5,5)
```

Double-buffered animation:

```basic
10 HGR : HCOLOR=3
20 SHOW 1                 : REM show page 1, draw on hidden page 2
30 HGR2 : REM (or clear+draw your frame on the hidden page)
40 VBL : SHOW 2           : REM flip: show page 2, draw on hidden page 1
50 GOTO 30
```

## Build / memory map

`.sketch.json` drives the DevBench build (`applesoft_gen2.cfg`, `extraAsm: io.s`;
`gen2gfx.inc`, `macros.s`, `zeropage.s` and the `dev/lib/gen2` scanline table are
`.include`d). Runs on POM1 preset 2 (GEN2 HGR Development Bench, 48 KB).

```
$0300-$031F  GEN2 state (cursor, colours, draw-page, CSW vector)
$0400-$07FF  TEXT / LO-RES page 1   (PRINT, GR, PLOT, HLIN, VLIN, SCRN)
$0800-$1FFF  BASIC program + vars   (HIMEM pinned at $2000)
$2000-$3FFF  HI-RES page 1          (HGR, HPLOT)
$4000-$5FFF  HI-RES page 2          (HGR2, SHOW)
$6000-....   interpreter image (run 6000R)
$FF00-$FFFF  WOZ Monitor (APRINT / Apple-1 ECHO $FFEF)
```

HIMEM is pinned at `$2000` so BASIC storage (`$0800–$1FFF`, 6 KB) can never grow
into the HGR framebuffer. Pinned by the `applesoft_gen2_smoke` ctest (core,
PRINT→GEN2, HGR/HGR2/lo-res draw, HOME/HTAB/VTAB, SCRN).

> Adding more tokens? Statements must stay contiguous below `$C0`; do additions
> in one renumber pass (operators index off `TOKEN_PLUS`, functions off
> `TOKEN_SGN`). Keywords that are prefixes of others must come first in the name
> table (e.g. `HGR2` before `HGR`).
