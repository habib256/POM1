# TODO

## Done (v1.1–v1.4)

- [x] P-LAB Apple-1 Graphic Card (TMS9918 VDP: 256×192, 15 colors, 32 sprites, Graphics I/II/Text/Multicolor, I/O at `$CC00`/`$CC01`)
- [x] Bundled P-LAB software: Tetris, TMS9918 demo suite, PicShow image viewer (`software/tms9918/`)
- [x] File browser shows `.bin` files; default binary load address `$0280`
- [x] Charmap ROM bitmap rendering (`charmap.rom`, 5×7 matrix, CRT glow)
- [x] Apple Cassette Interface (ACI ROM at `$C100`, I/O at `$C000`/`$C081`, live audio, `.aci`/`.wav` import/export)
- [x] Uncle Bernie's GEN2 Color Graphics Card (280×192 HIRES, NTSC artifact color, pixel glow)
- [x] HGR Maze program for GEN2 (Recursive Backtracker, 19×11 cells)
- [x] Memory Viewer inline double-click editing
- [x] cc65 linker config for GEN2 (`software/hgr/apple1_gen2.cfg`)

## Open

- [ ] **GEN2 higher-resolution maze**: 16-bit DFS with smaller pixel blocks (e.g., 34×23 cells). Non-byte-aligned rendering produces NTSC color artifacts instead of solid white walls — needs a rendering approach that works at sub-byte granularity.
- [ ] **More GEN2 programs**: image viewers, drawing tools, additional demos for the 280×192 HIRES display. (claude --resume 080eb60b-9589-49d2-a001-0f9610a49666)
- [ ] **Native file dialog**: File loading/saving currently uses built-in file browsers instead of system file pickers.

## Future extensions — P-Lab hardware & software ecosystem

Reference hub: [P-Lab](https://p-l4b.github.io/). The Graphic Card is done; other peripherals would need register maps and timing from the linked PDFs/schematics.

- [x] **P-Lab Apple-1 Graphic Card** ([graphic](https://p-l4b.github.io/graphic/)): TMS9918 VDP emulation — done in v1.4. Includes CodeTank-compatible software (Tetris, demos, PicShow).
- [x] **apple1-videocard-lib** — <https://github.com/nippur72/apple1-videocard-lib>: Pre-built binaries bundled in `software/tms9918/`.
- [ ] **More P-LAB TMS9918 software**: Compile and bundle additional demos (anagram, graphs, life, hello-world) from apple1-videocard-lib. Requires KickC compiler.
- [ ] **CodeTank daughterboard ROM**: Support the `apple1_jukebox` target (ROM at `$4000-$7FFF`) for programs stored on the CodeTank EEPROM.
- [ ] **Apple-1 Wi-Fi modem** ([wifi](https://p-l4b.github.io/wifi/)): Bridge emulated serial to host TCP/Telnet or WebSocket.
- [ ] **SID interface** ([A1-SID](https://p-l4b.github.io/A1-SID/)): 6581/8580-style audio — I/O decode and timing from PDFs/schematics.
- [ ] **microSD storage card** ([sdcard](https://p-l4b.github.io/sdcard/)): Virtual disk (host directory or image).
- [ ] **Misc programs reference (Angela / P-Lab)** ([angela](https://p-l4b.github.io/angela/)): Curated ports (Dobble, Oregon Trail, etc.).
