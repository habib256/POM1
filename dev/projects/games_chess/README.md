# Apple 1 Chess (text mode) — v0.4

Pure 6502 asm chess for the original Apple 1, text mode (40×24).
Inspired by StewBC/cc65-Chess but rewritten from scratch in the
POM1 game-port idiom (matches Sokoban / Connect 4 trilogies).

## Features (v0.4)

**New in v0.4:**
- **`M` — Cycle game mode** between four configurations:
  - HUMAN VS HUMAN
  - HUMAN (W) VS COMPUTER (B) — you play white, AI plays black
  - COMPUTER (W) VS HUMAN (B) — AI plays white, you play black
  - COMPUTER (W) VS COMPUTER (B) — full self-play
  The game loop checks the mode before each turn and auto-invokes the
  AI for the designated side.
- **Perft fix** — the v0.3 over-count bug is fixed. `is_attacked_runner`
  no longer clobbers `ce_piece`, so the move-gen dispatch stays
  consistent across iterations. `perft1` now returns the correct **20**
  legal moves for the initial position.

**v0.3 features (still here):**
- **AI 1-ply greedy** — type `A` for one-shot AI move; or use mode
  cycling (`M`) for continuous AI play.
- **Undo** — type `U` to reverse the last move (single level).
- **Perft** — type `P` to count legal moves at current position. Now
  matches expected values.

**Carry-over from v0.2:**

- Full 0x88 board representation
- All 6 piece types: pawn, knight, bishop, rook, queen, king
- Captures, promotion (auto-queen + user-selectable Q/R/B/N)
- **En passant** (full tracking + capture)
- **Castling** (kingside `E1G1`/`E8G8` and queenside `E1C1`/`E8C8`,
  also accepts `OO`/`OOO` syntax)
- Check detection (move that leaves own king in check is rejected)
- Checkmate / stalemate detection (brute-force scan after each move)
- Algebraic move input (`E2E4`, `E7E8Q`) — RETURN no longer required
  for non-promotion moves
- Two-player human vs human
- 2-char piece notation: `WP`/`BP`/`WN`/`BN`/`WB`/`BB`/`WR`/`BR`/`WQ`/`BQ`/`WK`/`BK`
- Chequered board pattern (`..` for dark empty squares, `  ` for light)

## Not yet implemented (v0.5+)

- AI alpha-beta 2-3 ply with MVV-LVA (planned: v1.2)
- Cursor input mode `C` (TMS9918/HGR variants, planned)
- 50-move and 3-fold repetition draws
- Interrupt key during AvA mode (currently must wait for game-over)
- Mode toggle `M` in TMS9918 / HGR variants (text-only for v0.4)

## Build

```
make
```

Produces three artefacts under `software/games/`:
- `Chess.bin.lo` (~2.6 KB) — lower-bank code at $0280-$0FFF
- `Chess.bin.hi` (~2.7 KB) — upper-bank engine + state at $E000-$EFFF
- `Chess.txt` — combined Wozmon-hex with both blocks + autorun

## Memory map (per Parmegiani's standard Apple-1 dual-bank)

```
$0000-$003F   Zero page
$0100-$01FF   6502 stack
$0200-$027F   Wozmon kbd buffer
$0280-$0FFF   CODE — variant code (renderer + I/O + main)         ← lower 4K
$E000-$EEFF   ENGINE — chess engine (move-gen + AI + perft)       ← upper 4K
$EF00-$EFFF   BOARDST — 0x88 board + state + AI/undo BSS          ← upper 4K
```

This matches the canonical 1976+ Apple-1 / Replica-1 RAM layout
(8 KB total, split into two 4 KB banks). The chess program does NOT fit
on a 4K-only Apple-1 (POM1 preset 0 is 4K-only).

## Run in POM1

```
./POM1                                       # any preset with the upper bank
File → Load Memory → software/games/Chess.txt
```

Wozmon will auto-run via the trailing `0280R`.

Or via CLI (loads both banks separately):

```
./POM1 --preset 1 --terminal --cpu-max \
       --load 0x0280:software/games/Chess.bin.lo \
       --load 0xE000:software/games/Chess.bin.hi --run 0x0280
```

**Recommended preset**: 1 (Apple-1 with ACI & Integer BASIC). POM1 lets
the chess engine overwrite the BASIC ROM region at $E000 with no fuss.
On real hardware you'd cold-start a Replica-1 without BASIC loaded and
type the lower bank in via the keyboard then `0280R`.

## Controls

At the **MOVE?** prompt:

| Input | Effect |
|---|---|
| `E2E4` | Move from E2 to E4 (no RETURN needed) |
| `E7E8Q` | Promote pawn at E7→E8 to queen (R/B/N also accepted) |
| `E1G1` | Kingside castle (white). For black: `E8G8`. |
| `E1C1` | Queenside castle (white). For black: `E8C8`. |
| `OO` / `OOO` | Alternative castling syntax (KS / QS). |
| `Q` | Quit current game (returns to splash) |
| `U` | Undo last move (single level) |
| `A` | Let the computer play this turn (1-ply greedy) |
| `M` | Cycle game mode (HvH / WAI / BAI / AvA) |
| `P` | Count legal moves at current position (perft) |
| `D` | Toggle AI search depth (not yet implemented) |
| `C` | Cursor input mode (TMS/HGR variants only — not in text) |

After checkmate or stalemate, press any key to start a new game.

## Architecture

This project is the **text-mode variant** of the Apple-1 Chess trilogy.
It links three 6502-asm objects:

1. `Chess.o` (this directory) — text-mode renderer + game loop
2. `chess_engine.o` (lib/chess/) — board, move-gen, make/unmake, check
3. `chess_text_io.o` (lib/chess/) — algebraic move parser

The TMS9918 variant (`dev/projects/tms9918_chess/`) and the HGR
variant (`dev/projects/hgr_chess/`) reuse the same `chess_engine.o`
+ `chess_text_io.o` and only swap the renderer (`Chess.o`).

## Known constraints

- Apple-1 keyboard forces uppercase. Move input must be uppercase
  (`E2E4`, not `e2e4`). Promotion piece letter likewise: `Q`/`R`/`B`/`N`.
- The text-mode 40×24 display has no cursor addressing; every move
  re-prints the whole board (the terminal scrolls old state off the
  top — period-authentic for 1976).
- Game-status detection (`game_status`) iterates all 64×64 from/to
  pairs after every move. At 1 MHz this takes a few hundred ms.
  Optimised in v1.2 with proper move-list iteration.
