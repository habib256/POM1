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
- [x] **P-LAB MODEM BBS** (Wi-Fi Modem): 65C51 ACIA at `$B000`-`$B003`, ESP8266 AT command interpreter, Hayes AT commands (ATDT host:port), TELNET IAC protocol, non-blocking TCP client, baud rate simulation (50–19200)
- [x] **P-LAB Terminal Card**: passive bidirectional serial bridge, TCP server on `localhost:6502`, 7-bit/8-bit modes, CTRL-O/I/T/L/R control commands, TELNET IAC handling
- [x] Bundled ACIA terminal program (`software/wifi/terminal.txt`) for keyboard↔modem bridge
- [x] MODEM BBS + Terminal Card combo for BBS access with native ANSI rendering in external terminal
- [x] `Memory::setKeyPressedRaw()` for lowercase/8-bit Terminal Card key injection
- [x] Toolbar reorganized: Load, SD Card, Cassette, SID, HGR, TMS9918, Terminal, BBS
- [x] Hardware menu reorganized: ACI Cassette Control + Bernie (classics) then P-LAB cards grouped (microSD, SID, TMS9918, Terminal, MODEM BBS)
- [x] Cassettes moved from `software/cassettes/` to `cassettes/` at project root
- [x] **SID converter v2**: instruction-aware patching (shared `INST_LENGTHS`), expanded CIA/VIC/indirect opcodes, neighbor-pair data table filtering, ESC-to-stop player, "APPLE1 P-LAB SID PLAYER" banner
- [x] **TMS9918+SID demo** (`tools/make_tms_sid_demo.py`): world's first Apple 1 TMS9918+SID combined program — Graphics II title screen + Streets of Rage 2 SID tune. Output: `software/tms9918/TMS_SID_Demo.bin`

## Open

- [ ] **SID: Arkanoid (Galway) does not play**: ISR detected at `$4086` but tune is silent. Galway's multi-ISR raster-split architecture needs more than static ISR detection.
- [ ] **SID: some IRQ-driven tunes fail**: ISR detection now covers LDA/LDX/LDY + STA/STX/STY patterns for `$FFFE`/`$0314` vectors. Players using computed or indirect ISR addresses (e.g., BMX Kidz) are still not detected.
- [x] **SID converter false positives** (v1.7.1): All passes now instruction-aware (`INST_LENGTHS` table). Pass 5 data table patching uses neighbor-pair filtering. Pass 2 expanded with indirect stores ($81/$91). Pass 3/4 expanded with indexed addressing.
- [ ] **SID: implement "PIANO" software**: Bundle/port the P-LAB “SID Keyboard Program” (PIANO) per the **Apple-1 SID Interface Addendum PDF** ([`A1-SID/APPLE-1_SID_PIANO.pdf`](https://p-l4b.github.io/A1-SID/APPLE-1_SID_PIANO.pdf)). Player **starts at `$0600`** (run via `0600R`) and uses keyboard mapping (notes: `Z X C V B N M ,` + sharps `S D G H J`; waveforms: `O` noise, `P` pulse, `T` triangle, `W` saw; octaves: numerals). Parameters are stored in RAM locations around `$0280` (ADSR, delay loops, PWM, etc.) and copied to SID registers at `$C800+`. Optional Theremin mode (`*`) reads paddles at `$C819/$C81A` (not necessarily supported in POM1 yet).
- [ ] **Applesoft (float) + microSD support**: Integrate [`nippur72/applesoft-lite-sdcard`](https://github.com/nippur72/applesoft-lite-sdcard) to provide an Applesoft BASIC variant with **floating-point** and **`LOAD`/`SAVE` via the P-LAB microSD interface**. Bundle the ROM/binary in the repo (e.g. under `software/basic/` or `roms/`), define the expected load/entry points, and ensure it coexists with POM1’s memory map (avoid ZP conflicts noted by the project). Add clear run instructions and a quick test plan (save a small program, reboot, load it back from `sdcard/`).
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
- [x] **Apple-1 MODEM BBS** ([wifi](https://p-l4b.github.io/wifi/)): 65C51 ACIA + TCP/TELNET — done in v1.7.
- [x] **Apple-1 Terminal Card** ([terminal](https://p-l4b.github.io/terminal/)): TCP server serial bridge — done in v1.7.
- [x] **P-LAB microSD Storage Card** ([sdcard](https://p-l4b.github.io/sdcard/)): 65C22 VIA + ATMEGA MCU protocol, virtual SD card via host `sdcard/` directory — done in v1.6.
- [ ] **Misc programs reference (Angela / P-Lab)** ([angela](https://p-l4b.github.io/angela/)): Curated ports (Dobble, Oregon Trail, etc.).
