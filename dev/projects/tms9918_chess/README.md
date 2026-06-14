# Apple 1 Chess (TMS9918 variant) — v0.1 (engine v0.5)

Pure 6502 asm chess for the original Apple 1 + P-LAB TMS9918 Graphic Card.
Two-player human vs human on an 8×8 board with 16×16-pixel piece glyphs
displayed on the TMS9918 screen; status text and move input on the
regular Apple-1 40×24 display.

Engine bumped to v0.5 — the AI no longer blunders (SEE on the destination
square, MVV-LVA tie-break, random reservoir sampler for genuine ties; see
`dev/lib/games/chess/README.md`). The text-variant v0.5 UX features (`H`, `L`,
last-move highlight, MAT line, anti-scroll, `D` strategy toggle) are
text-only and don't appear here — the TMS renderer prints `NOT IMPLEMENTED`
when the parser surfaces those return codes.

Inspired by StewBC/cc65-Chess (algorithm reference). Shares
`chess_engine.o` with the text and HGR variants of the trilogy
(`games_chess`, `hgr_chess`).

## Build

```
make
```

Produces `software/tms9918/TMS_Chess.bin` (~3.5 KB) and `TMS_Chess.txt`.

## Run

```
./POM1                                       # any preset works
File → Load Memory → software/tms9918/TMS_Chess.txt
```

The `software/tms9918/` directory auto-enables the TMS9918 card on load.

Or via CLI:

```
./POM1 --preset 7 --terminal --cpu-max \
       --load 0x0280:software/tms9918/TMS_Chess.bin --run 0x0280
```

## Display layout

The TMS9918 screen is 32×24 chars (Mode 1 = Graphics I, 8×8-pixel cells).
The 8×8 chess board is rendered as a 16×16 grid of cells (each board
square = 2×2 chars = 16×16 pixels).

```
        cols 5..7   cols 8..23      cols 24..31
       ┌─────────┬───────────────┬───────────────┐
rows   │ rank    │  board area   │ status (text) │
4..19  │ labels  │  (16x16 cells)│ overlay area  │
       │  1..8   │               │ (currently    │
       │         │               │  unused)      │
       └─────────┴───────────────┴───────────────┘
row 21:               file labels A..H
```

Colour groups (TMS9918 has one colour byte per group of 8 chars):

| Group | Chars | Use |
|---|---|---|
| 0 | 0-7 | Invisible (background) |
| 1 | 8-15 | Light board square |
| 2 | 16-23 | Dark board square (stippled) |
| 4-6 | 32-55 | White piece glyphs (24 chars = 6 pieces × 4) |
| 8-10 | 64-87 | Black piece glyphs (same shapes, inverted colour) |

Piece glyphs are 16×16 pixel art (4 chars in a 2×2 block per piece).
v0.1 ships with simple silhouettes; v1.0 will add more recognisable
icons (crowns for king/queen, mitre for bishop, etc.) and v1.1 may add
chequered light/dark cell shading.

## Controls

Same as the text variant (`games_chess`):

| Input | Effect |
|---|---|
| `E2E4` + RET | Move from E2 to E4 |
| `E7E8Q` + RET | Promote pawn at E7→E8 to queen |
| `Q` | Quit current game |

## Mutex with other cards

The TMS9918 in POM1 conflicts at the bus level with:
- A1-AUDIO SE (different I/O range but evicts the TMS9918)
- A1-SID at $CC00-$CC01 (TMS9918 wins via priority 10)

Recommended preset for chess: **#8 P-LAB Apple-1 with TMS9918** (which
also auto-plugs the CodeTank daughterboard, but that doesn't conflict
with chess running from $0280-$3FFF).

## Status — v0.1

| Feature | State |
|---|---|
| Engine: full move-gen, captures, promotion, check, mate | ✅ |
| TMS9918 board rendered with chequered pattern | ✅ |
| Piece glyphs 16×16 (simple silhouettes) | ✅ |
| Per-move incremental redraw | ✅ |
| Cursor input mode (H/J/K/L) | ⏳ v0.3 |
| AI (alpha-beta 2-3 ply) | ⏳ v1.2 |
| En passant, castling | ⏳ v0.2 |
