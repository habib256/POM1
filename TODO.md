# TODO

This file tracks **open** work only. For shipped features, see git log
or the version notes referenced in `README.md`.

## Open

- [ ] **SID: Arkanoid (Galway) does not play** — ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than the converter's static ISR detection.
- [ ] **SID: some IRQ-driven tunes still fail** — Players using computed or indirect ISR addresses (e.g. BMX Kidz) escape the LDA/LDX/LDY + STA/STX/STY pattern matcher.
- [x] **SID: implement "PIANO" software** — Bundled as `software/sid/piano.bin` (1104 bytes, `$0600-$0A4F`) and `sdcard/PIANO#060600`. Load via SD CARD OS: `RUN PIANO` or direct: `0600R`. Keyboard: `Z X C V B N M ,` + sharps `S D G H J`; waveforms `O P T W`; octaves `1-8`; `*` Theremin. Source: `software/sid/piano.asm`.
- [x] **Applesoft Lite P-LAB microSD variant** — Bundled as `roms/applesoft-lite-microsd.rom` (8192 B, SHA-256 `e50756f6…`): P-LAB [APPLESOFT-FT.zip](https://p-l4b.github.io/terminal/APPLESOFT-FT.zip) (*Fast Terminal patched APPLESOFT BASIC for SD OS 1.2*). `Memory::loadApplesoftLite()` loads it at `$6000-$7FFF` when microSD is on and CFFA1 is off; CFFA1 presets still use `applesoft-lite-cffa1.rom` at `$E000-$FFFF`. Cold/warm: `6000R` / `6003R`; persistence also via SD shell `8000R` → `ASAVE` / `LOAD` / `RUN` (`#F8` tags).
- [ ] **Uncle Bernie's Improved ACI** — Emulate the ACI Extended firmware page at `$C500-$C5FF` (256 B, started with `C500R`). Adds EOR checksum verification (`R`/`W`), extended format with 8-byte header at `$07F8-$07FF` (`RX`/`WX`), and autostart (`<from>` == `<to>`). Compatible Apple II checksum. **Firmware is proprietary and unpublished** — Uncle Bernie stated on Applefritter: *"The improved firmware I wrote for it will never be published."* PROMs were distributed only with his physical IC kits (now sold out). **Action required:** contact Uncle Bernie via [Applefritter PM](https://www.applefritter.com/user/254186/track) to request cooperation; precedent: he offered to share docs with the HoneyCrisp emulator developer. POM1 already emulates his GEN2 HGR card, which is a good argument. Code-side when ROM obtained: load at `$C500`, update `CassetteDevice` for extended format (header + checksum). Ref: [Applefritter thread](https://www.applefritter.com/content/uncle-bernies-improved-apple-1-cassette-interface), [ACI improvements comparison](https://www.applefritter.com/content/which-aci-improvements-do-exist-and-work).
- [x] **GEN2 higher-resolution maze** — Bundled as `software/hgr/HGR2_Maze.asm` and `HGR2_Maze.txt` (1323 bytes, `$0280-$07AA`). 34×23 cells (782 cells, 16-bit DFS) with 4×4 pixel blocks. Sub-byte rendering via lookup tables (`col_byte`, `col_mask1`, `col_mask2`) for non-byte-aligned HGR writes — all wall pixels have adjacent neighbors, resolving to solid white via NTSC artifact coloring. Grid at `$4000` (page-aligned), DFS stacks at `$4400`/`$4800`. Load via `280R`.
- [ ] **More GEN2 programs** — image viewers, drawing tools, additional 280×192 HIRES demos.
- [x] **GEN2 Sokoban** — Bundled as `software/hgr/HGR6_Sokoban.asm` and `HGR6_Sokoban.txt` (7399 bytes, 98% of 7552-byte budget, `$0280-$1F60`). Classic push-boxes puzzle with 20×12 grid of 14×16 byte-aligned tiles (7 tile types: floor/wall/target/box/box-on-target/player/player-on-target). Uses existing `hgr_lo/hgr_hi` tables from `hgr_tables.inc` for scanline addresses. State grid at `$4000` (240 bytes, one byte per cell). Delta rendering redraws only 2-4 affected tiles per move. **72 levels**: 3 teaching + Microban I #1..#65 and #67..#70 by David W. Skinner (2000) for progressive difficulty (Microban #66 skipped, too tall: 9×14). ASCII level format (`#.$@*+`). Startup prompts for keyboard layout (1=QWERTY WASD, 2=AZERTY ZQSD); up/left keys are parameterised in zero-page so only two comparisons change. Win condition: all targets show `TILE_BOX_TARGET` (rejects tile 2 and tile 6). Other controls: R reset, N next. Load via `280R`.
- [x] **Text Sokoban** — Bundled as `software/games/Sokoban.asm` and `Sokoban.txt` (4054 bytes, `$0280-$1255`). Pure Apple 1 40×24 text version: same mechanics and state grid logic as the GEN2 port, but rendering is ASCII (`# . $ * @ +`). Per-frame output is minimal: blank line, 12 grid rows, blank line, footer `UP/DN/LT/RT R=RESET N=NEXT` — the terminal scrolls naturally between frames (no explicit clear, no repeated header). 47 levels: 3 teaching + Microban I #1..#44. Uses new `software/apple1_4k.cfg` (CODE `$0280`, size `$1000` = 4 KB) — a reusable mid-size config for text programs that need more than `apple1.cfg` but no HGR RAM. Same keyboard layout prompt (QWERTY WASD / AZERTY ZQSD). Load via `280R`.
- [x] **TMS9918 Sokoban** — Bundled as `software/tms9918/TMS_Sokoban.asm` and `TMS_Sokoban.txt` (4552 bytes, `$0280-$1447`). P-LAB Apple-1 Graphic Card version: Graphics I mode, **full-screen 32×24** display with 16×16-pixel tiles (each Sokoban tile = a 2×2 block of 8×8 chars). 7 tile types, one per "colour group" (bases 0, 8, 16, 24, 32, 40, 48) so each tile type gets its own palette colour (grey wall, red target, yellow box, light-green box-on-target, light-blue player, medium-green player-on-target, transparent floor). Playfield enlarged from 20×12 to **16×12** tiles — all 47 packed Microban levels (widths ≤ 15) still fit; smaller levels are centred at runtime (init_level ignores stored 20-grid offsets and recomputes `row_off/col_off` from `NROWS/NCOLS`). Delta rendering: each move writes 4 char codes (TL, TR, BL, BR) at the right 2×2 name-table position. 47 levels (3 teaching + Microban I #1..#44). Apple 1 text screen carries title, layout prompt (QWERTY/AZERTY) and win messages. Requires the TMS9918 card to be enabled in the Hardware menu. Load via `280R`.
- [x] **Connect 4 — three ports** — Two-player drop-piece game, 7×6 grid. Identical state/logic across modes (shared `drop_piece`, `check_win` scanning 4 directions via `check_4_at_x` with parameterised offset, XOR #3 player toggle). Controls: keys 1-7 drop into a column, R restarts. Pieces: "red" and "yellow". Three builds:
    - **Text**: `software/games/Connect4.asm` + `Connect4.txt` (1016 bytes, `apple1.cfg`). 7×6 grid drawn with `|.|R|Y|` ASCII, one frame per move — terminal scrolls naturally.
    - **HGR**: `software/hgr/HGR7_Connect4.asm` + `HGR7_Connect4.txt` (2003 bytes, `apple1_gen2.cfg`). 14×16 rounded-rectangle pieces in NTSC artefact colour (red→orange, yellow→green). Board centred at byte col 13, scanline 48. Delta-draw the newly dropped piece only.
    - **TMS9918**: `software/tms9918/TMS_Connect4.asm` + `TMS_Connect4.txt` (1116 bytes, `apple1_gen2.cfg`). Graphics I mode; board of blue 8×8 slots (bg=4) with circular fg cut-outs centred at name-table (row 9, col 12). Three colour groups: black hole (empty), red (player 1), yellow (player 2) — same pattern bitmap reused across groups.
- [x] **Sokoban — undo + move counter** — shipped in all three variants (April 2026).
    - **Shared refactor** — plumbing lives in `software/games/sokoban_common.inc` (ascii_to_tile, leave/enter/enter_as_box tables, check_win, wait_key, print_str_ax, and `load_level` which RLE-decodes into a 256-byte scratch buffer at `$4100`). Level data is RLE-compressed in `sokoban_levels.inc` (1..47) and `sokoban_levels_ext.inc` (48..72). The RLE encoder is `tools/sokoban_rle.py` (format: byte<$80 literal, byte>=$80 ⇒ (byte & $7F) copies of the next byte).
    - **Text version** (`software/games/Sokoban.asm`, 3453 B): full undo + move counter in footer (`MOVES: NNN   U=UNDO  R=RESET  N=NEXT`).
    - **HGR version** (`software/hgr/HGR6_Sokoban.asm`, 6200 B, 72 levels): undo with delta-redraw of the 2-3 affected tiles. Reclaimed the `cur_x/cur_y/mul_tmp/mul_res0` zero-page slots from unused `hgr_tables.inc` helpers; aliased them back so the include still assembles.
    - **TMS9918 version** (`software/tms9918/TMS_Sokoban.asm`, 3929 B, 47 levels): same undo logic with TMS delta-redraw (4 char codes per tile via `draw_cell`).
    - **Remaining work** (optional, not blocking):
        1. **State grid relocation from `$4000`** — for the stock-RAM Apple 1 (text variant only). Pick an address in `$0000-$0FFF`, e.g. `$0E00-$0EEF`, so the text Sokoban runs without extended RAM. `LEVEL_BUF` would need to move (240-byte scratch).
        2. **Display the moves counter in HGR/TMS variants.** Internally tracked but not shown — would need an Apple-1-text HUD line that refreshes without scrolling the screen.
- [ ] **CodeTank daughterboard ROM** — support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.
- [ ] **Misc programs reference (Angela / P-Lab)** — curated ports (Dobble, Oregon Trail, etc.) from [angela](https://p-l4b.github.io/angela/).
- [ ] **Native file dialog** — file loading/saving still uses the in-app browser instead of system file pickers.

## Technical debt & code quality (audit April 2026)

### Performance
- [ ] **Snapshot RAM copy 64 KB @ 60 Hz** (`EmulationController::publishSnapshotLocked()`) — full RAM copy under `stateMutex` every frame, ≈ 3.8 MB/s wasted. → Double-buffer + atomic pointer swap, or page-level dirty tracking.
- [ ] **`float tmpBuf[512]`** in `AudioDevice.cpp` — too small; audio callbacks can request up to 2048 frames and the excess is silently truncated. → Pre-allocated member buffer sized dynamically.
- [ ] **TMS9918 VRAM 16 KB copied per frame** (`TMS9918::copySnapshot`) even when the card is disabled. → `vramDirty` flag, copy only on change.
- [ ] **`std::vector<quint8>(0x10000)` allocated per frame** for `EmulationSnapshot` in `MainWindow_ImGui.cpp`. → Persistent pre-allocated snapshot, swap pointers, publish only CPU registers + RAM pointer.

### Thread safety
- [ ] **Race on `runRequested`** (`EmulationController.cpp:565`) between `.load()` and `cpu->run()`: a `stopCpu()` call can be missed. → Capture state under mutex before running the slice.
- [ ] **Implicit lock order `stateMutex` → `keyMutex`** (`processQueuedKeysLocked()`) is undocumented; future inversion could deadlock. → Annotate `REQUIRES(stateMutex)` or merge into a single thread-safe queue.
- [ ] **`Screen_ImGui::instance` is a non-atomic static pointer** (`Screen_ImGui.cpp:21`) — race on concurrent access. → `std::atomic<Screen_ImGui*>` or pass `this` via opaque context.

### Architecture
- [ ] **`Memory` is a god object** — 15+ peripheral `unique_ptr`s, matching enable flags, inline I/O dispatch. → Extract a `PeripheralBus` with dynamic registry; `Memory` only manages the address space.
- [ ] **`EmulationController` SRP violation** — 50+ public methods (CPU, ROMs, snapshots, keyboard, tape, reset…). → Split into `SaveStateManager`, `KeyboardController`, `RomLoader`.
- [ ] **`MainWindow_ImGui.cpp` is monolithic** (~2500 lines) — UI + events + state + dialogs + rendering in one class. → Per-dialog classes; machine presets in external JSON/YAML.
- [ ] **Static `Screen_ImGui::displayCallback`** couples UI to emulation. → `DisplayDevice` interface injected into `Memory`; `Screen_ImGui` implements it.

### Network & peripherals
- [ ] **`connectToHost()` with no timeout** (`WiFiModem.cpp`) — slow networks can block the emulation thread indefinitely. → Non-blocking socket + `select()`/`poll()` with explicit timeout.
- [ ] **Silent ACIA Rx overrun** (`WiFiModem.cpp`, `TerminalCard.cpp`) — overflowed bytes are dropped; the real ACIA's overrun bit is not emulated. → Set the Receiver Overrun flag in STATUS.
- [ ] **Sockets not RAII-wrapped** — an exception between `socket()` and `close()` leaks the FD. → `SocketHandle` class with destructor.

### Code quality
- [ ] **`sscanf` without validation** (`MemoryViewer_ImGui.cpp:71,79,240`) — vulnerable to malformed input. → `std::from_chars`.
- [ ] **Archaic `typedef`** (`Memory.h:37-38`) — `typedef uint8_t quint8` → `using quint8 = uint8_t`.
- [ ] **Direct `std::cout`/`cerr`** — no level filtering, can't redirect to the debug UI. → Minimal `Logger` interface, dependency-injected.
- [ ] **`ma_device` allocated via `new`** (`AudioDevice.cpp:154`). → `std::unique_ptr<ma_device>` with custom deleter.
- [ ] **`stringBuffer` without `reserve()`** (`MicroSD.cpp`) — repeated reallocations during MCU response accumulation. → `stringBuffer.reserve(256)` in the constructor.

### Tests
- [ ] **No unit tests** — refactors risk regressing 6502 emulation silently. → Klaus Dormann's 6502 functional tests as the first smoke test; add GTest or Catch2.
- [ ] **Parsers not fuzzed** — `loadHexDump()` and `executeATCommand()` accept external input untested. → LibFuzzer targets via a CMake option (`ENABLE_FUZZING`).
