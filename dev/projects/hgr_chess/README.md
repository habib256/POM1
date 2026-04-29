# Apple 1 Chess (GEN2 HGR variant) — v0.1

Pure 6502 asm chess for the original Apple 1 + Uncle Bernie's GEN2
Color Graphics Card. Two-player human vs human, with the chess engine +
piece display on the regular Apple-1 40×24 text screen, AND a chequered
8×8 board frame drawn on the GEN2 HGR framebuffer.

Inspired by StewBC/cc65-Chess (algorithm reference). Shares
`chess_engine.o` with the text and TMS9918 variants of the trilogy.

## v0.1 design rationale

This variant ships in two stages:

- **v0.1 (current):** GEN2 HGR draws the empty chequered board frame
  alongside the text-mode game. Pieces are rendered as ASCII letters on
  the text screen (same layout as the text variant). This proves the
  HGR card preset works with the chess engine without waiting for the
  ~860 B of piece-tile pixel art that v1.1 will add.
- **v1.1 (planned):** 21×22-pixel piece tiles drawn directly on HGR;
  text screen will only carry the status line.

Until v1.1 lands, the visual difference between this variant and the
text variant is the chequered board pattern visible on the HGR display
(the GEN2 framebuffer at $2000-$3FFF).

## Build

```
make
```

Produces `software/hgr/HGR_Chess.bin` and `HGR_Chess.txt`.

## Run

```
./POM1
File → Load Memory → software/hgr/HGR_Chess.txt
```

The `software/hgr/` directory auto-enables the GEN2 card on load.

Or via CLI:

```
./POM1 --preset 13 --terminal --cpu-max \
       --load 0x0280:software/hgr/HGR_Chess.bin --run 0x0280
```

## Controls

Same as the text and TMS9918 variants. See `dev/projects/games_chess/README.md`.

## Mutex with other cards

GEN2 HGR claims `$2000-$3FFF` for its framebuffer. This conflicts with:

- A1-IO RTC (`$2000-$200F` VIA — preset-level mutex)
- Juke-Box ROM-32 jumper extends to $4000-$BFFF (no overlap with $2000-$3FFF
  but the chess engine's BSS lives at $1F00 which is fine)

Recommended preset for chess: **#13 Uncle Bernie's Apple-1 with GEN2 HGR**.

## Status — v0.1

| Feature | State |
|---|---|
| Engine: full move-gen, captures, promotion, check, mate | ✅ |
| HGR chequered board frame (8×8 = 168×176 px) | ✅ |
| Pieces drawn on text screen (chequered ASCII) | ✅ |
| Pieces drawn directly on HGR (21×22 px tiles) | ⏳ v1.1 |
| Cursor input mode (H/J/K/L on HGR) | ⏳ v1.2 |
| AI (alpha-beta 2-3 ply) | ⏳ v1.2 |
| En passant, castling | ⏳ v0.2 |

## Known v0.1 limitations

- The HGR board uses a simplified linear scanline LUT, not the proper
  Apple II non-linear addressing. Visual artefacts on the chequered
  pattern are expected. Will be replaced with `dev/lib/hgr/hgr_tables.inc`
  in v1.1.
- The text display still has the dotted board pattern (mirroring the
  text variant). Set `crtScanlineAlpha = 0.0` in POM1 to maximise text
  contrast if needed.
