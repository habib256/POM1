# TODO

## Done (v1.1–v1.6)

- [x] P-LAB Apple-1 Graphic Card (TMS9918 VDP: 256×192, 15 colors, 32 sprites, Graphics I/II/Text/Multicolor, I/O at `$CC00`/`$CC01`)
- [x] Bundled P-LAB software: Tetris, TMS9918 demo suite, PicShow image viewer (`software/tms9918/`)
- [x] File browser shows `.bin` files; default binary load address `$0280`
- [x] Charmap ROM bitmap rendering (`charmap.rom`, 5×7 matrix, CRT glow)
- [x] Apple Cassette Interface (ACI ROM at `$C100`, I/O at `$C000`/`$C081`, live audio, `.aci`/`.wav` import/export)
- [x] Uncle Bernie's GEN2 Color Graphics Card (280×192 HIRES, NTSC artifact color, pixel glow)
- [x] HGR Maze program for GEN2 (Recursive Backtracker, 19×11 cells)
- [x] Memory Viewer inline double-click editing
- [x] cc65 linker config for GEN2 (`software/hgr/apple1_gen2.cfg`)
- [x] **P-LAB A1-SID Sound Card** (MOS 6581/8580): 3 voices, ZDF SVF filter, ADSR with delay bug, 4× oversampling, digi playback, I/O at `$C800`-`$CFFF`
- [x] **AudioDevice**: central audio mixer (miniaudio / Web Audio), decoupled from CassetteDevice
- [x] **SID converter** (`tools/sid2apple1.py`): PSID/RSID → Apple 1 `.bin`, 7 patching passes, IRQ-driven ISR detection, `--batch`, `--all-songs`, `--hex`
- [x] **30 bundled SID tunes** from HVSC (Hubbard, Galway, Tel, Daglish, Huelsbeck) including 5 IRQ-driven
- [x] **Auto-enable hardware cards** when loading from `software/sid/`, `software/hgr/`, `software/tms9918/`
- [x] Krusader ROM no longer loaded by default — `$A000-$BFFF` is free User RAM
- [x] **P-LAB microSD Storage Card**: 65C22 VIA at `$A000`-`$A00F`, ATMEGA MCU protocol, SD CARD OS ROM (8KB) at `$8000`-`$9FFF`, DIR/LS/CD/LOAD/SAVE/READ/WRITE/DEL/MKDIR/RMDIR/PWD/MOUNT
- [x] Virtual SD card maps host `sdcard/` directory — tagged filenames (`NAME#TTAAAA`), fuzzy LOAD matching
- [x] Protocol robustness: string buffer limit (256 B), write size limit (32 KB), DIR timeout (500K cycles), stale response auto-abort, conditional debug logging

## Open

- [ ] **SID: Arkanoid (Galway) does not play**: ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than static ISR detection.
- [ ] **SID: some IRQ-driven tunes fail**: ISR detection relies on `LDA #xx / STA $FFFE` patterns. Players using computed or indirect ISR addresses (e.g., BMX Kidz) are not detected.
- [ ] **SID converter false positives**: Byte-scan passes (VIC `$D0xx`, data tables) can NOP legitimate code in edge cases. Residual-`$D4` warnings help but manual review may be needed.
- [ ] **GEN2 higher-resolution maze**: 16-bit DFS with smaller pixel blocks (e.g., 34×23 cells). Non-byte-aligned rendering produces NTSC color artifacts instead of solid white walls — needs a rendering approach that works at sub-byte granularity.
- [ ] **More GEN2 programs**: image viewers, drawing tools, additional demos for the 280×192 HIRES display.
- [ ] **Native file dialog**: File loading/saving currently uses built-in file browsers instead of system file pickers.

## Future extensions — P-Lab hardware & software ecosystem

Reference hub: [P-Lab](https://p-l4b.github.io/). The Graphic Card, SID, and microSD Storage Card are done; other peripherals would need register maps and timing from the linked PDFs/schematics.

- [x] **P-Lab Apple-1 Graphic Card** ([graphic](https://p-l4b.github.io/graphic/)): TMS9918 VDP emulation — done in v1.4.
- [x] **P-Lab A1-SID Sound Card** ([A1-SID](https://p-l4b.github.io/A1-SID/)): 6581/8580-style audio — done in v1.5.
- [x] **apple1-videocard-lib** — <https://github.com/nippur72/apple1-videocard-lib>: Pre-built binaries bundled in `software/tms9918/`.
- [ ] **More P-LAB TMS9918 software**: Compile and bundle additional demos (anagram, graphs, life, hello-world) from apple1-videocard-lib. Requires KickC compiler.
- [ ] **CodeTank daughterboard ROM**: Support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.
- [ ] **Apple-1 Wi-Fi modem** ([wifi](https://p-l4b.github.io/wifi/)): Bridge emulated serial to host TCP/Telnet or WebSocket.
- [x] **P-LAB microSD Storage Card** ([sdcard](https://p-l4b.github.io/sdcard/)): 65C22 VIA + ATMEGA MCU protocol, virtual SD card via host `sdcard/` directory — done in v1.6.
- [ ] **Misc programs reference (Angela / P-Lab)** ([angela](https://p-l4b.github.io/angela/)): Curated ports (Dobble, Oregon Trail, etc.).
