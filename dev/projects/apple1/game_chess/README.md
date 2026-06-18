# Apple 1 Chess (text mode) — v0.5

*[← POM1 documentation index](../../../../doc/README.md)*

Pure 6502 asm chess for the original Apple 1, text mode (40×24).
Inspired by StewBC/cc65-Chess but rewritten from scratch in the
POM1 game-port idiom (matches Sokoban / Connect 4 trilogies).

## Features (v0.5)

**New in v0.5 — playability & UX overhaul:**

- **AI no longer blunders.** A new SMART strategy adds Static Exchange
  Evaluation on the destination square, MVV-LVA tie-break between
  equal-scored captures, and an 8-bit LFSR reservoir sampler for genuine
  ties. The AI now refuses to give up a queen for a defended pawn, and two
  AvA runs diverge by move 2–3 instead of replaying the same game.
- **`D` — Toggle AI strategy** between NAIVE (v0.4 behaviour, useful for
  comparison) and SMART (default — SEE + MVV-LVA + random tie-break).
- **`H` — Hint.** The engine proposes a move without playing it. (Note:
  this consumes the single-level undo slot; the player's prior `U` is
  invalidated. Documented limitation, fixed in v1.2 with multi-level undo.)
- **`L` — List legal moves.** Prints every legal move for the side to
  move, 8 per line in algebraic notation. Helpful for newcomers.
- **Anti-scroll redraw.** Every board redraw pushes the previous state
  fully off the 24-line screen (Apple-1 has no cursor addressing). The
  current board lands at the top, no more scroll-pollution.
- **Last-move highlight.** The first glyph char of `mv_from` and `mv_to`
  is replaced with `*`. Visible without inverse video. Cleared on undo
  and on hint.
- **Material balance line** (`MAT W:+N` / `MAT B:+N`) printed under the
  board only when one side is ahead — suppressed entirely while material
  is equal so the slow Apple-1 display doesn't pay for a no-info line on
  every redraw of the opening / quiet middle game.
- **"COMPUTER THINKING..." message** — printed once before the AI searches
  so the ~300 ms freeze at 1 MHz isn't silent. (An earlier per-candidate `.`
  dot was dropped: at ~10 ms each it cost more than the search it narrated.)

**Carry-over from v0.4:**

- **`M` — Cycle game mode** between four configurations:
  HUMAN VS HUMAN / HUMAN(W) VS AI(B) / AI(W) VS HUMAN(B) / AI VS AI.
- **`A` — AI plays this turn** (one-shot trigger).
- **`U` — Undo** the last move (single-level).
- **`P` — Perft** count of legal moves at current position.

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

## Not yet implemented (v0.6+)

- AI alpha-beta 2-3 ply (planned: v1.2 — needs ply-stacked `saved_*`)
- Cursor input mode `C` (TMS9918/HGR variants, planned)
- 50-move and 3-fold repetition draws (planned: v0.6)
- Insufficient-material draw detection (planned: v0.6)
- Multi-level undo (would also let `H` not consume the undo slot)
- Interrupt key during AvA mode (currently must wait for game-over)
- v0.5 UX features (`H`/`L`/highlight/MAT/anti-scroll) in TMS9918/HGR
  variants — these reuse this directory's renderer.

## Build

```
make
```

Produces three artefacts under `software/Apple-1 games/`:
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
on a 4K-only Apple-1 (POM1 preset 3 is 4K-only).

## Run in POM1

```
./POM1                                       # any preset with the upper bank
File → Load Memory → software/Apple-1 games/Chess.txt
```

Wozmon will auto-run via the trailing `0280R`.

Or via CLI (loads both banks separately):

```
./POM1 --preset 4 --terminal --cpu-max \
       --load 0x0280:software/Apple-1 games/Chess.bin.lo \
       --load 0xE000:software/Apple-1 games/Chess.bin.hi --run 0x0280
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
| `A` | Let the computer play this turn |
| `M` | Cycle game mode (HvH / WAI / BAI / AvA) |
| `D` | Toggle AI strategy (NAIVE / SMART) — v0.5 |
| `H` | Hint: AI suggests a move without playing it — v0.5 |
| `L` | List legal moves for the side to move — v0.5 |
| `P` | Count legal moves at current position (perft) |
| `C` | Cursor input mode (TMS/HGR variants only — not in text) |

After checkmate or stalemate, press any key to start a new game.

## Architecture

This project is the **text-mode variant** of the Apple-1 Chess trilogy.
It links three 6502-asm objects:

1. `Chess.o` (this directory) — text-mode renderer + game loop
2. `chess_engine.o` (lib/games/chess/) — board, move-gen, make/unmake, check
3. `chess_text_io.o` (lib/games/chess/) — algebraic move parser

TMS9918 and HGR variants existed historically (each reused the shared
`chess_engine.o` + `chess_text_io.o`, swapping only the renderer); they
are no longer in-tree as of 2026. The text variant here is the
canonical reference.

## Known constraints

- Apple-1 keyboard forces uppercase. Move input must be uppercase
  (`E2E4`, not `e2e4`). Promotion piece letter likewise: `Q`/`R`/`B`/`N`.
- The text-mode 40×24 display has no cursor addressing. v0.5 emits
  24 leading CRs before each redraw to push the previous board fully
  off the screen — visually equivalent to a clear-screen.
- Game-status detection (`game_status`) iterates all 64×64 from/to
  pairs after every move. At 1 MHz this takes a few hundred ms.
  Optimised in v1.2 with proper move-list iteration.
- The `H` (hint) command consumes the single-level undo slot. After
  a hint the player's prior `U` is no longer available. Multi-level
  undo (v1.2) will fix this.
- v0.5 UX features (highlight, MAT, H, L, anti-scroll, COMPUTER THINKING
  message) live in `Chess.asm` and don't propagate to the HGR / TMS9918 variants.
  Those variants benefit from the stronger AI via the shared engine.

## Tests

There is **no automated ctest for the chess engine yet** — perft(1)=20 for
the initial position is verified manually for now. The plumbing for a future
test is already in place: the Makefile emits `Chess.sym` (VICE label file via
`ld65 -Ln`) alongside the binary, and the engine exposes `init_board` and
`perft1` (which leaves the count in `perft_count_lo:hi`). A smoke test would
load the binary, parse `Chess.sym`, drive the 6502 through `init_board` then
`perft1`, and assert the count is exactly 20.

Until that test lands, exercise the engine manually in POM1 (any 1-ply move
that changes the count signals a regression in move generation, the
`is_attacked_runner / atk_piece` aliasing, or the perft enumeration loop).
