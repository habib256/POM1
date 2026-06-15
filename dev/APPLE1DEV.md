# APPLE1DEV.md

Agent-facing playbook for writing **new Apple 1 software** that runs under POM1.

**Companion docs** (link instead of duplicating):

| Doc | Role |
|-----|------|
| [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) | Référence ASM détaillée (6502, cc65, texte, HGR, TMS9918) — tirée des trilogies Sokoban + Connect 4. |
| [`SILICONBUGS.md`](SILICONBUGS.md) | TMS9918 vs silicium réel, strict timing, sprites — **obligatoire** avant d’optimiser des boucles VRAM. |
| [`TODO6502.md`](TODO6502.md) | Backlog logiciel 6502 / `dev/projects`. |
| [`doc/CLI.md`](../doc/CLI.md) | Table complète des flags CLI (`CliDispatcher.cpp`). |
| [`CLAUDE.md`](../CLAUDE.md) | Architecture émulateur (mutex bus, orchestration, `ctest`). |
| [`README.md`](../README.md) | Guide utilisateur, tableau des presets, index logiciels. |

**Sommaire :** sections **§1–§12** numérotées ci-dessous (parcourir les titres).

---

## 1. Decision tree — pick your stack

| Goal | Language | Mode | Linker cfg |
|------|----------|------|-----------|
| Quick demo, one-screen output | Integer BASIC | Text 40×24 | — |
| Game with strings, floats, save | **Applesoft Lite** | Text (no cursor pos) | — |
| Fast tile gameplay, any mode | **6502 asm (cc65)** | Text / HGR / TMS9918 / GT-6144 | see §4 |
| Apple-1-movie text mode | asm | Text | `dev/cc65/apple1_4k.cfg` |
| Colour pixel art, fractals | asm + GEN2 | HGR 280×192 | `dev/cc65/apple1_gen2.cfg` |
| Sprite/tile game, multi-colour | asm + TMS9918 | Graphics I 32×24 | `dev/cc65/apple1_4k.cfg` (VRAM off-bus). Do not assume GEN2 + TMS9918 on one board — preset-level mutex except Multiplexing Fantasy (`README.md` / `CLAUDE.md`). |
| 1976 SWTPC graphics | asm + GT-6144 | 64×96 mono | `dev/projects/gt6144_hello/gt6144.cfg` |
| SID jingle | asm @ `$C800` regs | — | any |
| Shell tool, file manager | asm + microSD shell | Text | `dev/cc65/apple1_4k.cfg` |
| **Prefer C?** Text program | **C (cc65)** | Text 40×24 | `dev/cc65/apple1_c.cfg` + `dev/lib/apple1c/` |
| **Prefer C?** Colour pixel art | C + GEN2 | HGR 280×192 | `dev/cc65/apple1_gen2_c.cfg` + `dev/lib/gen2c/` |
| **Prefer C?** Sprite/tile game | C + TMS9918 | Graphics I/II | `apple1-videocard-lib/cc65/codetank_c.cfg` |

> **Writing in C?** All three C targets share one card-neutral Apple-1
> text/keyboard base (`dev/lib/apple1c/` — `woz_puts` / `woz_getkey` / `woz_mon`),
> with the graphics runtime (`gen2c` for GEN2, the videocard-lib for TMS9918)
> layered on top. Full guide: [`Programming_Apple1_C.md`](Programming_Apple1_C.md).

**Base addresses** (stick to these for canonical `.txt` dumps):
- `$0280` — ACI / microSD / Juke-Box / Applesoft programs (default `SAVE`/`LOAD` target, universal run address)
- `$0300` — alternate when `$0280-$02FF` is reserved
- `$0800` — Applesoft program storage (`SAVE` writes the tokenised listing here)
- `$E000-$EFFF` — Integer BASIC ROM, off-limits

---

## 2. Toolchain fast-path

**Install cc65 first** (the `ca65` / `ld65` / `cl65` suite): `sudo apt install cc65`
(Debian/Ubuntu) · `sudo dnf install cc65` (Fedora) · `sudo pacman -S cc65` (Arch) ·
`brew install cc65` (macOS) · <https://cc65.github.io/> (Windows/other). Verify with
`ca65 --version`.

**Easiest authoring loop:** the in-app **POM1 Bench** (*DevBench → POM1 Bench*,
desktop only) edits, assembles/compiles (asm **or** C) and runs in one click, with
a `HELLO WORLD` starter per target — no Makefile needed. Copy `dev/projects/_template/`
for a minimal asm or C starting point. The manual flow below is for batch/CI.

Per-project Makefiles under `dev/projects/<name>/` already wire `ca65` + `ld65` + Woz-hex emit. Manual flow if you need it:

```bash
ca65 -o build/MyProg.o dev/projects/<name>/MyProg.asm
ld65 -C dev/cc65/apple1_4k.cfg -o build/MyProg.bin build/MyProg.o
python3 -c "
data = open('build/MyProg.bin','rb').read(); base = 0x0280
for i in range(0, len(data), 16):
    print(f'{base+i:04X}: ' + ' '.join(f'{b:02X}' for b in data[i:i+16]))
" > software/<dir>/MyProg.txt
# In POM1: File > Load Memory → MyProg.txt → `280R` in Wozmon
```

Compiled `.bin` / `.txt` Woz hex always land under `software/<dir>/` — that's POM1's runtime tree (preset auto-enable hooks are wired to `software/Graphic HGR/`, `software/Graphic TMS9918/`, etc.).

All cc65 configs reserve **`$0000-$0022` ZP** (35 bytes); Wozmon + ACI claim the rest. `Memory::loadHexDump()` accepts canonical Wozmon dumps: `AAAA: HH HH …` lines, optional `:` separator, `R` suffix at EOF for auto-run, `T` prefix (turbo, no per-char delay), inline `//` `#` `;` comments stripped (otherwise mnemonic letters like `LDA`/`DEX` would parse as data).

---

## 3. Apple 1 I/O cheat sheet

| Address | Name | Role |
|---------|------|------|
| `$D010` | KBD | Last key, **bit 7 always set** — strip with `AND #$7F` |
| `$D011` | KBDCR | Bit 7 = 1 when key ready; reading `$D010` clears the strobe |
| `$D012` | DSP | Write a char (**bit 7 must be set** — `ORA #$80`); read bit 7 = 0 when ready |
| `$FFEF` | ECHO | Wozmon routine: print A (expects bit 7 set) |
| `$FFFC-$FFFF` | Vectors | Reset / IRQ / NMI in Wozmon ROM |

**Bit-7 rule** — PIA 6821 uses bit 7 as a data-valid strobe both ways. Write: `ORA #$80` first; `CR` is `$8D`, not `$0D`. Read: `AND #$7F`. **Uppercase only** — keyboard forces it in hardware. Compare `#'W'`, never `#'w'`.

```asm
wait_key:
@wk:    LDA KBDCR
        BPL @wk           ; bit 7 clear → no key
        LDA KBD
        AND #$7F
        RTS
```

---

## 4. Display modes — one paragraph each (then go to Programming_Apple1_ASM.md)

### Text 40×24 (default)
Append-only terminal, no cursor addressing. To "refresh", reprint the frame — scroll does the work. Min viable game frame ≈ 12×20 chars + footer fits in 24 lines. `ECHO` at `$FFEF` with `ORA #$80`. See `dev/projects/games_sokoban/Sokoban.asm`.

### GEN2 HGR (280×192, Uncle Bernie)
Framebuffer **`$2000-$3FFF`** (8 KB, non-linear Apple II layout). 7 px/byte; **bit 7 = NTSC group selector**, not a pixel. Isolated lit pixel = colour (violet/green/blue/orange depending on group + screenX parity); **adjacent lit pixels = white**. Use `dev/lib/hgr/hgr_tables.inc` (`plot_pixel`, `clear_hgr`, scanline tables). Byte-aligned tile widths (7/14/21/28 px) avoid sub-byte work; for arbitrary widths use the sub-byte mask LUT pattern (see `dev/projects/hgr_maze/HGR_Maze.asm`). **Full reference: `dev/Programming_Apple1_ASM.md` §5.**

### TMS9918 (256×192, P-LAB Graphic Card)
I/O at `$CC00` (data) + `$CC01` (control). VRAM is **separate** (16 KB, I/O-only). Graphics I = 32×24 cells of 8×8 px. Layout: pattern `$0000`, name `$1800`, colour `$2000` (**one colour byte per group of 8 chars** — exploit by placing each tile type at char `0, 8, 16, …`). **Must disable sprites on init** (write `$D0` to first sprite-Y at `$1B00`) or garbage appears. **VBlank sync = polling recommandé** (`BIT $CC01 / BPL` or the `WAIT_VBLANK` macro in `lib/tms9918/tms9918.inc`) — P-LAB câble bien /INT → /IRQ (vérifié par Parmigiani), mais le polling est plus simple et portable ; l'IRQ-on-VBlank marche si tu installes un handler au vecteur `$FFFE` lisant `$CC01` atomiquement (voir [`SILICONBUGS.md`](SILICONBUGS.md) Bug N°2). **Silicon strict** drops too-fast VRAM writes (`--silicon-strict` / Hardware menu — [`doc/CLI.md`](../doc/CLI.md)); tuning & pad helpers → SILICONBUGS §2 / §17, `dev/lib/tms9918/tms9918_pad.asm`. **Full ASM tutorial: [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) §6.**

### SWTPC GT-6144 (64×96 mono, 1976 — first commercial Apple-1 graphics card)
**Write-only** I/O at `$D00A`, 4-phase FSM on a single byte:
- `0..63` → latch X + pixel **OFF**
- `64..127` → latch X + pixel **ON**
- `128..223` → commit **Y** (actual plot uses latched X + state)
- `224..255` → control opcode (`byte & 0x07`: 0=invert, 1=normal, 4=unblank, 5=blank)

No read-back, no main-RAM framebuffer (lives in 6× Intel 2102 SRAM). Plot `(x,y)` ON: `STA $D00A` with `x|64`, then `y|128`. Inversion + blanking affect video path only. Power-on = visible bistable noise — clear before drawing. Examples: `dev/projects/gt6144_hello/`, `dev/projects/gt6144_life/`. Linker: `dev/projects/gt6144_hello/gt6144.cfg`.

---

## 5. Integer BASIC vs Applesoft Lite

**Integer BASIC** (`$E000`, cold-start `E000R`): 16-bit signed (-32 767…+32 767), no floats, no strings beyond `PRINT`, ~16-deep `GOSUB`, no disk I/O (cassette or Juke-Box `&` only). Use when small + integer-only. Examples in `software/Integer_basic/` (cold-start once with `E000R`, then File > Load Memory a `.apl.txt` — each ends with `E2B3R` to re-enter BASIC with the program intact — then `RUN`).

**Applesoft Lite** (`$6000` with microSD preset, cold-start `6000R`, warm `6003R`; or `$E000` with CFFA1):

| Supported | Notes |
|---|---|
| `GET A$` | Single-key read, no Enter — arcade-style loops |
| `CLS` | Scroll-clear (24 CRs), not a real clear |
| `SAVE "NAME"` / `LOAD "NAME"` | Routes to `sdcard/NAME#F80801` with microSD on |
| `POKE` / `PEEK` / `CALL` | E.g. `POKE &HC800,…` to drive SID directly |
| `RND`, `INT`, `ABS`, `CHR$`, `ASC`, `LEN`, `MID$`, `LEFT$`, `RIGHT$`, `STR$`, `VAL` | Standard |
| `PRINT`, `INPUT`, `IF…THEN`, `FOR…NEXT`, `GOTO`, `GOSUB`, `ON…GOTO`, `DATA`/`READ` | Standard |

**NOT supported** — no `HOME`/`HTAB`/`VTAB`/`TAB`/`POS`, no `INVERSE`/`FLASH`/`NORMAL`, no `COS`/`SIN`/`TAN`/`ATN`. **Design consequence**: never build a grid UI in Applesoft. Build turn-driven scroll-text games — each turn prints a fresh block, the terminal scrolls old state off the top. Period-authentic for 1977.

---

## 6. SID sound — `$C800-$CFFF`, 29 regs (`addr & 0x1F`)

Three voices, each 7-register block:

| Offset | V1 | V2 | V3 | Role |
|--------|----|----|----|------|
| +0 | `$C800` | `$C807` | `$C80E` | Frequency LSB |
| +1 | `$C801` | `$C808` | `$C80F` | Frequency MSB |
| +2 | `$C802` | `$C809` | `$C810` | PWM LSB |
| +3 | `$C803` | `$C80A` | `$C811` | PWM MSB (4 bits) |
| +4 | `$C804` | `$C80B` | `$C812` | Control: gate + waveform (`01` triangle, `02` saw, `04` pulse, `08` noise) |
| +5 | `$C805` | `$C80C` | `$C813` | Attack / Decay |
| +6 | `$C806` | `$C80D` | `$C814` | Sustain / Release |

Global: `$C818` volume + filter mode, `$C815-$C817` filter cutoff/resonance, `$C819`/`$C81A` paddle inputs.

```asm
        LDA #$10           ; master volume = 1 (0..15)
        STA $C818
        LDA #$08            \ A=0, D=8
        STA $C805           /
        LDA #$F8            \ S=$F, R=8
        STA $C806           /
        LDA #<freq
        STA $C800
        LDA #>freq
        STA $C801
        LDA #$41            ; gate on + triangle
        STA $C804
```

From Applesoft: `POKE &HC818,16 : POKE &HC805,8 : POKE &HC804,65`.

Reference: `dev/projects/sid_piano/Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm`. **C64 `.sid` conversion**: `tools/sid2apple1.py` rewrites `$D400` → `$C800`, neutralises CIA/VIC, emits `.bin` for `$0280`. Source tunes at [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection).

---

## 7. Peripherals — user-facing commands

Help → Hardware Reference is authoritative; this is a quick-glance.

### SD CARD OS (microSD, `8000R`)

| Command | Effect |
|---|---|
| `D` / `LS` | List cwd |
| `CD <dir>` / `CD ..` | **Only navigation primitive** — absolute `/PATH`, relative, or `..` |
| `PWD` | Print cwd (prompt already shows it: `/PLAB/MCODE>`) |
| `LOAD <name>` | Fuzzy case-insensitive prefix; reads at the tagged address |
| `SAVE` / `WRITE` | Write a memory range to cwd |
| `DEL <name>` | Delete in cwd |
| `MKDIR` / `RMDIR` | Create / remove sub-dir in cwd |

**Invariant**: every name-accepting command resolves against `currentDirectory` only — no recursion. Use `CD` to navigate first. **Tagged filename**: `NAME#TTAAAA` (TT = `06` binary, `F1` Integer BASIC, `F8` Applesoft; AAAA = hex load address). Example: `ACEYDUCEY#f10800`.

### Juke-Box Program Manager (`BD00R`, `&` prompt)

`H` help · `D` list current page · `L<X>` load tagged letter X · `P<0-F>` page switch (paged Flash) · `B` drop into BASIC (`E2B3R`, non-destructive) · `X` exit to Wozmon. Sub-menu **Save Program** at `B800R` (`#` prompt): `W` write RAM range, `S` save BASIC, `L` back to PM. **Building a Juke-Box ROM**: `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` — produces the 32 KB image with `$A5` signature at file offset `$7D00` (= `$BD00`). Don't use P-LAB's `2-packer.sh` (subtly different layout).

### MODEM BBS (Hayes subset, desktop only)

`AT` / `ATDT host:port` / `ATH` / `ATE0/1` / `ATI` / `ATZ`. `+++` (1 s guard) → command mode. Typical:

```
0280R                         ; load ATmodem from software/net/, start ACIA bridge
ATDT BBS.FOZZTEXX.COM:23
... (talk) ...
+++  → ATH
```

### Terminal Card (TCP loopback `:6502`, desktop only)

7-bit default (CR→CRLF, uppercase-in `Ctrl-I`, uppercase-out `Ctrl-O`); `Ctrl-T` → 8-bit raw (PETSCII / UTF-8 BBS). `Ctrl-L` clear, `Ctrl-R` reset, `Ctrl-S` screenshot. ESC-prefixed alternates (`ESC T/O/L/R/I/S`) for ttys eating raw control chars.

### A1-IO & RTC (`$2000-$200F` VIA — mutex with GEN2 HGR)

24-reg broadcast on 100-cycle period with PORTB STROBE. Regs 0-5 = RTC (H/M/S/D/M/Y), reg 6 = DS3231 die temp, ADC + digital in/out follow. Demo: `dev/projects/a1io_rtc_clock/RtcClock.asm`.

### SWTPC PR-40 (passive `$D012` sniffer, Jobs 1976)

Every byte `STA $D012` (with bit 7 set, normal display rules) also lands in PR-40's 40-char FIFO. CR (`$8D`) or full FIFO flushes one line; each flush arms ~0.8 s mechanical cycle holding PB7 high. *Mixed* mode: CPU naturally stalls at Wozmon's `BIT $D012 / BMI`. *PrintOnly* mode: CPU floods FIFO at 1 MHz (PB7 ignores video /RDA). Nothing special from asm.

---

## 8. Deployment — four channels

1. **Memory load** (default for dev iteration) — ship `.txt` Woz hex or `.bin`, user does **File > Load Memory** then `280R`. The Load dialog auto-enables the matching card from the file's folder: `software/Graphic HGR/` (GEN2), `software/SOUND SID/` (A1-SID), `software/Graphic TMS9918/` or `software/Apple-1_TMS_CC65/` (TMS9918), `software/Graphic gt-6144/` (GT-6144), `software/a1io_rtc/` (A1-IO & RTC), `software/NET/` (Wi-Fi modem), or `sdcard/` (microSD). Each match also pops the card's window.
2. **microSD tagged file** — drop `NAME#TTAAAA` into `sdcard/` (optionally a sub-dir users `CD` into). Persists across sessions, also in WASM (preloaded MEMFS).
3. **Juke-Box ROM bundle** — rebuild `roms/jukebox.rom` with your program baked, pick preset #11, type `BD00R`, choose from `&` prompt.
4. **Cassette tape** — dump capture as `.aci`/`.wav`/`.mp3`/`.ogg`, drop in `cassettes/`. Add a line in `cassettes/tapeinfo.txt` (`MYPROG.ogg = 0280.04FF`) so the deck jaquette prints *"Type 0280.04FFR"*. Works in pulse mode (ACI plugged) and audio-stream mode (firmware-less).

Applesoft programs: `SAVE "NAME"` writes `sdcard/NAME#F80801` directly — no manual dump.

---

## 9. Testing

### Manual

```bash
./POM1 --preset 5 --terminal --cpu-max     # microSD + Applesoft Lite + telnet :6502
# File > Load Memory → my.txt → `280R` (or whatever start)
```

### Telnet harness

`tools/test_*_telnet.py` drives the emulator via Terminal Card. Two patterns: **agent auto-launches** POM1 (e.g. `test_aci_telnet.py`, `test_sdcard_subdir_navigation_telnet.py`) — run from repo root, suitable for CI/regression — or **user runs POM1 separately** and the script just connects to `127.0.0.1:6502`.

Skeleton:

```python
import select, signal, socket, subprocess, time
from pathlib import Path

HOST, PORT, CTRL_R = "127.0.0.1", 6502, 18
REPO = Path(__file__).resolve().parent.parent

def recv_avail(sock, total=4.0, idle=0.4):
    end = time.time() + total; buf = b""
    while time.time() < end:
        r, _, _ = select.select([sock], [], [], idle)
        if r:
            chunk = sock.recv(65536)
            if not chunk: break
            buf += chunk
        elif buf: break
    return buf.decode("latin-1", errors="replace")

def send(sock, cmd, wait=0.3, t=4.0):
    sock.sendall((cmd + "\r").encode("ascii")); time.sleep(wait)
    return recv_avail(sock, total=t)

proc = subprocess.Popen([str(REPO/"build"/"POM1"), "--preset", "5", "--terminal", "--cpu-max"],
                       stdout=open("/tmp/pom1.log","w"), stderr=subprocess.STDOUT,
                       start_new_session=True)
time.sleep(3.0)   # boot + 15-frame card defer
sock = socket.create_connection((HOST, PORT), timeout=5)
recv_avail(sock, total=2.0)
sock.sendall(bytes([CTRL_R])); time.sleep(0.9)         # soft reset to Wozmon
out = send(sock, "8000R", wait=0.6, t=3.0)             # enter SD CARD OS
proc.send_signal(signal.SIGTERM); proc.wait(timeout=5)
```

Full example: `tools/test_sdcard_subdir_navigation_telnet.py`.

### CLI one-liners

| Goal | Command |
|---|---|
| microSD + Applesoft + telnet, MAX speed | `./POM1 --preset 5 --terminal --cpu-max` |
| Load + run + drive | `./POM1 -p 1 --terminal --load 0300:prog.bin --run 0300 --paste keys.txt` |
| Swap cards | `./POM1 -p 13 --disable hgr --enable sid --sid-chip 8580 --speed 34091` |
| Seed microSD fixture | `./POM1 -p 5 --sd-mkdir BASIC --sd-put host/PROG#F80801:BASIC/PROG#F80801` |
| Capture SID to `.wav` | `./POM1 -p 6 --rec --save-tape /tmp/out --save-tape-format wav` |
| Step + BRK trace | `./POM1 -p 0 --trace-brk --step 10` |
| Freeze RTC | `./POM1 -p 9 --rtc-freeze "2000-01-01 00:00:00"` |

Complete verb table (all phases): [`doc/CLI.md`](../doc/CLI.md).

Repeating flags stack in CLI order. Card overrides (`--enable`, `--disable`, `--sid-chip`, `--jukebox-jumper`) land **before** the 15-frame deferred plug — clean. Verbs needing a plugged card (`--paste`, `--play`, `--rec`, `--sd-*`, `--rtc-freeze`) run **after** the defer — always see the fully-initialised bus.

### Emulator-level invariant tests

Add a C++ test in `tests/`. Template: `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test`, no framework needed. See `CLAUDE.md` *Testing* for registered smoke tests and `ctest -N`.

---

## 10. Common gotchas

| Gotcha | Fix |
|---|---|
| Branch ±127 out of range | Invert + `JMP`, or trampoline label mid-routine |
| `ADC` picks up stale carry | `CLC` before every *first* `ADC` of a sum |
| Helper does `TAX` + `LDA tbl,X`, clobbers caller's X | Use `TAY` in helpers — X is reserved for `STA arr,X` callers |
| Array > 256 B via `LDA arr,Y` | `(zp),Y` with high-byte adjusted, or parallel lo/hi tables |
| `.include` path | Relative to source file, not CWD — keep `.inc` next to the `.asm` (or use Makefile `-I`) |
| Lit HGR pixel renders coloured not white | Need a lit neighbour. 1-px line = colour; 2-px = white |
| TMS9918 random sprites at boot | Write `$D0` to sprite-Y at `$1B00` to kill the chain |
| Apple 1 text "stalls" during a burst | `$D012` has terminal-speed delay; either poll bit 7 or autopipeline |
| `$D012` writes eat budget in telnet tests | `--cpu-max` + `Ctrl-T` (8-bit raw); fundamental fix is a Wozmon-free dropper |
| Applesoft has no grid because no HOME/VTAB | Embrace scroll-text — period-authentic 1977 |
| microSD `LOAD YUM` says `FILE NOT FOUND` | YUM lives under `sdcard/PLAB/MCODE/` — `CD PLAB` then `CD MCODE` first |

---

## 11. Example index — copy from these

| Want… | Copy from |
|---|---|
| Text-mode game, ASCII tiles | `dev/projects/games_sokoban/Sokoban.asm` |
| Text-mode BASIC | `software/Integer_basic/mini-startrek.apl.txt` (Integer) or write fresh Applesoft |
| HGR pixel plotter | `dev/projects/hgr_mandelbrot/HGR_Mandelbrot.asm` + `dev/lib/hgr/hgr_tables.inc` |
| HGR byte-aligned tiles | `dev/projects/hgr_sokoban/HGR_Sokoban.asm` (14 px wide) |
| HGR sub-byte tiles (≠ 7 px) | `dev/projects/hgr_maze/HGR_Maze.asm` (4-px walls) |
| HGR shape drawing | `dev/projects/hgr_house/HGR_House.asm` |
| TMS9918 multi-colour tiles | `dev/projects/tms9918_sokoban/TMS_Sokoban.asm` (colour-group trick) |
| TMS9918 full-screen board | `dev/projects/tms9918_connect4/TMS_Connect4.asm` (32×32 px pieces) |
| GT-6144 plotter | `dev/projects/gt6144_hello/GT1_Hello.asm`, `dev/projects/gt6144_life/GT1_Life.asm` |
| SID direct register play | `dev/projects/sid_piano/Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm` |
| SID from C64 conversion | `python3 tools/sid2apple1.py Music.sid` |
| RTC / sensors | `dev/projects/a1io_rtc_clock/RtcClock.asm` |
| Shared logic across modes | `dev/lib/games/sokoban/sokoban_*.inc` (mode-neutral routines), `dev/lib/games/chess/chess_engine.asm` (separately-linked engine .o) |
| Separately-linked engine module | `dev/lib/games/chess/chess_engine.asm` + per-variant Makefile linking 3 `.o` (text/TMS9918/HGR all share the same `chess_engine.o`) |
| Algebraic move parser | `dev/lib/games/chess/chess_text_io.asm` (parses 4-5 char input like `E2E4`, `E7E8Q`) |
| New linker config | `dev/cc65/apple1_4k.cfg` (4 096 B) / `apple1_gen2.cfg` (7 552 B, reserves HGR fb) / `pom1_fantasy.cfg` (Multiplexing Fantasy preset) |

---

## 12. Done-checklist

1. Fits the linker config? `ls -l build/*.bin` vs CODE size.
2. ZP usage in `$0000-$0022`?
3. Every `CMP` uses uppercase letters?
4. WASD prompt: QWERTY/AZERTY `1`/`2` selector with `key_up_code` / `key_left_code` in ZP?
5. Branches in range? (ca65 catches at assembly time.)
6. Both intermediate states handled (e.g. *player on target*)?
7. Loaded via File > Load Memory, watched at least one full loop?
8. If user-scripted, minimal `test_*_telnet.py` passes?
9. Distribution channel decided (memory load / microSD / Juke-Box / cassette)?
10. Entry added to `README.md` *Software Library* if shipping?
