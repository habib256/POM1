# TODO

## Done (v1.1–v1.3)

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

Reference hub: [P-Lab Apple-1 Graphic Card](https://p-l4b.github.io/graphic/) (priority — see first item). Other P-Lab pages document real peripherals; emulation would need register maps and timing from the linked PDFs/schematics.

- [ ] **P-Lab Apple-1 Graphic Card + CodeTank daughterboard** ([graphic](https://p-l4b.github.io/graphic/)): Primary interest — study English/Italian project PDFs, schematic, and BOM for memory-mapped I/O, framebuffer organization, and CodeTank coprocessor role. Cross-check with [Nippur72’s apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib) for software conventions and demos. Possible POM1 work: optional card toggle, RAM window emulation, and ImGui render path (distinct from Uncle Bernie GEN2 at `$2000–$3FFF`).
- [ ] **Apple-1 Wi-Fi modem** ([wifi](https://p-l4b.github.io/wifi/)): User manual and 9600 baud firmware notes — bridge emulated serial to host TCP/Telnet or WebSocket; Replica/512×4 PROM compatibility PDFs for behavioral quirks if matching real hardware.
- [ ] **I/O board & real-time clock** ([A1-IO_RTC](https://p-l4b.github.io/A1-IO_RTC/)): ATmega-based expansion (digital/analog I/O, DS18B20, LCD routines in docs) — define minimal register model if software in the wild depends on it; optional host time source for RTC registers.
- [ ] **SID interface** ([A1-SID](https://p-l4b.github.io/A1-SID/)): 6581/8580-style audio — I/O decode and timing from PDFs/schematics; optional low-level SID core or stub that satisfies jukebox/demo ROM expectations where documented.
- [ ] **microSD storage card** ([sdcard](https://p-l4b.github.io/sdcard/)): Block or file-layer protocol from user manual — optional virtual disk (host directory or image) and compatibility notes for Replica-1 / IEC add-on behavior if relevant to Apple-1 software.
- [ ] **Fast serial terminal** ([terminal](https://p-l4b.github.io/terminal/)): High-speed serial terminal hardware — alternate input/output path vs. stock PIA `$D010–$D012`; patched INTEGER/Applesoft listings may inform baud assumptions and escape sequences for a future serial window.
- [ ] **Misc programs reference (Angela / P-Lab)** ([angela](https://p-l4b.github.io/angela/)): Curated ports (Dobble, Oregon Trail, MAB, Angela, Mandelbrot, etc.) with ProDOS-tagged binary naming — no new hardware per se; optional inclusion of listings/binaries under `software/` or cross-links in docs for users pairing POM1 with SD-card workflows.
