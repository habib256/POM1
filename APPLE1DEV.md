# APPLE1DEV.md

Agent-facing playbook for writing **new Apple 1 software** that runs under POM1. Complements three other sources:

- **`doc/Programming_Apple1_ASM.md`** — 700-line deep dive on 6502 / cc65 / HGR / TMS9918 (French, by Arnaud, based on the Sokoban and Connect 4 trilogies). This file is the authoritative ASM reference — don't duplicate it, link to it when you hit "how do I draw a tile on HGR?".
- **`README.md`** — user-facing walkthrough, preset table, bundled-software index.
- **`CLAUDE.md`** — emulator-side architecture (what owns what, mutex order, ROM-load invariants).

The memory records at `/Users/factory/.claude/projects/-Users-factory-src-POM1/memory/` also contain hard-won lessons (`reference_apple1_game_programming.md`, `reference_subbyte_rendering.md`, `feedback_6502_register_preservation.md`) — read them before writing your first 6502 helper.

---

## 1. Decision tree — pick your stack

**What is the user asking for?**

| User goal | Language | Mode | Linker cfg |
|-----------|----------|------|-----------|
| Quick demo, toy, one-screen output | Integer BASIC | Text 40×24 | — (`.apl.txt`) |
| Interactive game, strings, floats | **Applesoft Lite** | Text (no cursor pos) | — (SAVE/LOAD via microSD) |
| Fast gameplay, tile-based, any mode | **6502 asm (cc65)** | Text / HGR / TMS9918 | see §4 |
| "Looks like the Apple 1 movie" | asm + text mode | Text | `apple1.cfg` |
| Colour pixel art, fractals, HGR demos | asm + GEN2 | HGR 280×192 | `hgr/apple1_gen2.cfg` |
| Sprite-driven game, multi-colour | asm + TMS9918 | Graphics I 32×24 | `apple1_gen2.cfg` (TMS VRAM is separate) |
| SID chip music / jingle | asm @ `$C800` regs | — | any |
| Shell tool, file manager | asm + microSD shell | Text | `apple1.cfg` |

**Base address conventions** (stick to these so `.apl.txt` dumps stay canonical):

- `$0280` — ACI / microSD / Juke-Box / Applesoft programs (default `SAVE`/`LOAD` target, and the universal run address)
- `$0300` — alternate when `$0280-$02FF` is reserved for something else
- `$0800` — Applesoft BASIC program storage (`SAVE` writes the tokenised listing starting here)
- `$E000-$EFFF` — **Integer BASIC ROM** — don't load user code here unless you *are* BASIC

---

## 2. Toolchain fast-path

```bash
# 1. Assemble
ca65 -o build/MyProg.o software/.../MyProg.asm

# 2. Link
ld65 -C software/apple1.cfg -o build/MyProg.bin build/MyProg.o

# 3. Convert .bin → Woz Monitor hex dump (.txt) that POM1's File > Load Memory accepts
python3 -c "
data = open('build/MyProg.bin','rb').read()
base = 0x0280
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    print(f'{base+i:04X}: ' + ' '.join(f'{b:02X}' for b in chunk))
" > software/.../MyProg.txt

# 4. Load in POM1 via File > Load Memory, then 280R in Woz Monitor
```

All cc65 configs define `ZP: $0000-$0022` (35 bytes of usable zero page — the Woz Monitor and ACI claim the rest).

**Why the `.apl.txt` hex-dump format?** POM1's `Memory::loadHexDump()` accepts the canonical Apple 1 Wozmon format:
- `AAAA: HH HH HH ...` — bytes at address `AAAA`
- `:` separator between 8-byte groups (historical 1976 dump style) works too
- `AAAAR` suffix at EOF runs at that address when the emulator finishes loading
- `T` prefix = turbo (don't simulate Wozmon's per-char delay)
- Inline comments `//`, `#`, `;` are stripped (prevents mnemonic letters like `LDA` / `DEX` being parsed as data)

---

## 3. Apple 1 I/O cheat sheet

| Address | Name | Role |
|---------|------|------|
| `$D010` | KBD | Last key typed, **bit 7 always set** — strip with `AND #$7F` |
| `$D011` | KBDCR | Bit 7 = 1 when a key is ready; reading `$D010` clears the strobe |
| `$D012` | DSP | Write a character (**bit 7 must be set** — `ORA #$80`). Read bit 7 = 0 when ready |
| `$FFEF` | ECHO | Woz Monitor routine: print A to the screen (expects bit 7 set) |
| `$FFFC-$FFFF` | Vectors | Reset / IRQ / NMI in the Woz Monitor ROM |

**Bit-7 rule** — the PIA 6821 uses bit 7 as a data-valid strobe in both directions:
- Writing: set bit 7 (`ORA #$80`) before `STA $D012` or `JSR ECHO`. `CR` is `$8D`, not `$0D`.
- Reading: strip bit 7 (`AND #$7F`) after `LDA $D010`.

**Uppercase-only** — the Apple 1 keyboard forces uppercase in hardware. Compare against `#'W'`, not `#'w'`.

**Standard wait-key** loop:

```asm
wait_key:
@wk:    LDA KBDCR
        BPL @wk           ; bit 7 clear → no key, spin
        LDA KBD
        AND #$7F
        RTS
```

---

## 4. Display modes — one-paragraph each (then go to the ASM doc)

### Text 40×24 (default)
Append-only terminal. No cursor addressing. To "refresh" just reprint the frame — the scroll does the work. Minimum viable game frame ≈ 12 rows × 20 chars + footer = fits in 24 lines easily. Use `ECHO` at `$FFEF` with `ORA #$80`. See `software/games/Sokoban.asm`.

### GEN2 HGR (280×192, Uncle Bernie)
Framebuffer at **`$2000-$3FFF`** (8 KB, non-linear Apple II scanline layout). 7 px/byte; **bit 7 selects the NTSC group** (not a pixel). Isolated lit pixel = colour (violet/green/blue/orange depending on group + screenX parity). **Adjacent lit pixels = white.** Use `software/hgr/hgr_tables.inc` for `plot_pixel`, `clear_hgr`, and the scanline address tables. For walls / tiles, prefer byte-aligned widths (7/14/21/28 px); for other widths use the sub-byte-mask LUT (`reference_subbyte_rendering.md` + `software/hgr/HGR2_Maze.asm`). **Full HGR reference: `doc/Programming_Apple1_ASM.md` §5.**

### TMS9918 (256×192, P-LAB Graphic Card)
I/O at `$CC00` (data) + `$CC01` (control). VRAM **is separate from main RAM** (16 KB, accessed only by I/O). Graphics I mode = 32×24 character cells, 8×8 px. Key VRAM layout: pattern table `$0000`, name table `$1800`, colour table `$2000` (**one colour byte per group of 8 chars** — exploit this for multi-colour tile games by placing each tile type at char `0, 8, 16, …`). **Must disable sprites** on init (write `$D0` to the first sprite-Y byte `$1B00`) or garbage appears. **Full TMS9918 reference: `doc/Programming_Apple1_ASM.md` §6.**

---

## 5. Integer BASIC — text-mode, integer only

Loaded at `$E000`; cold-start `E000R`. This is Woz / Apple BASIC 1976-era.

- **No floats, no strings beyond `PRINT`.** Arithmetic is 16-bit signed (-32 767..+32 767).
- No `GOSUB` return stack depth beyond ~16 levels.
- No disk I/O in the language itself — save/load programs via **cassette** or the **Juke-Box `&` prompt**.
- `LIST`, `RUN`, `NEW`, `PRINT`, `INPUT`, `LET`, `GOTO`, `GOSUB`, `IF … THEN`, `FOR … NEXT`, `DIM`.

Examples ship as `.apl.txt` Woz hex dumps in `software/basic/` (load via **File > Load Memory**, then `E2B3R` to re-enter BASIC with the program intact). **Use Integer BASIC when the program is small and integer-only.** Anything needing strings, floats, trig, or persistent storage → Applesoft Lite.

---

## 6. Applesoft Lite — text-mode, with floats + strings + microSD save

Loaded at **`$6000`** with the microSD preset (`6000R` to cold-start, `6003R` warm) or at `$E000` with CFFA1.

**Supported (use freely):**

| Feature | Notes |
|---|---|
| `GET A$` | Single-key read, no Enter required — enables arcade-style loops |
| `CLS` | Clears via 24 CRs (scroll-clear, not a real clear) |
| `SAVE "NAME"` / `LOAD "NAME"` | With microSD on, P-LAB routes these straight to `sdcard/NAME#F80801` |
| `POKE` / `PEEK` / `CALL` | Direct memory access — use `POKE &HC800,…` to drive the SID |
| `RND`, `INT`, `ABS`, `CHR$`, `ASC`, `LEN`, `MID$`, `LEFT$`, `RIGHT$`, `STR$`, `VAL` | All standard |
| `PRINT`, `INPUT`, `IF…THEN`, `FOR…NEXT`, `GOTO`, `GOSUB`, `ON…GOTO`, `DATA`/`READ` | Standard |

**NOT supported** (design around these):

- `HOME`, `HTAB`, `VTAB`, `TAB`, `POS` — **no cursor addressing**. Every frame = fresh block scrolled below the previous one. This is period-authentic for 1977-era Apple BASIC games.
- `INVERSE`, `FLASH`, `NORMAL` — no character attributes.
- `COS`, `SIN`, `TAN`, `ATN` — no trig (use polynomial approximations or lookup tables).

**Design consequence**: never build a grid/board UI. Build turn-driven scroll-text games — each turn prints one block of text, the terminal scrolls old state off the top. Embrace it.

**Saving from the emulator to the host:** the user types `SAVE "FOO"` at the Applesoft prompt → P-LAB writes `sdcard/FOO#F80801` (`F8` = Applesoft, `0801` = load address). The file shows up immediately on disk.

---

## 7. SID sound — `$C800-$CFFF`, 29 registers (`addr & 0x1F`)

Three voices, each at a 7-register offset:

| Offset | Voice 1 | Voice 2 | Voice 3 | Role |
|--------|---------|---------|---------|------|
| +0 | `$C800` | `$C807` | `$C80E` | Frequency LSB |
| +1 | `$C801` | `$C808` | `$C80F` | Frequency MSB |
| +2 | `$C802` | `$C809` | `$C810` | PWM LSB |
| +3 | `$C803` | `$C80A` | `$C811` | PWM MSB (4 bits) |
| +4 | `$C804` | `$C80B` | `$C812` | Control (gate + waveform: `01` triangle, `02` sawtooth, `04` pulse, `08` noise) |
| +5 | `$C805` | `$C80C` | `$C813` | Attack / Decay |
| +6 | `$C806` | `$C80D` | `$C814` | Sustain / Release |

Global: `$C818` volume+filter-mode, `$C815-$C817` filter cutoff+resonance, `$C819/$C81A` paddle / "Theremin" inputs.

**Play a note from asm** (voice 1):

```asm
        LDA #$10
        STA $C818             ; master volume = 1 (0..15)
        LDA #$08
        STA $C805             ; attack=0, decay=8
        LDA #$F8
        STA $C806             ; sustain=$F, release=8
        LDA #<freq
        STA $C800
        LDA #>freq
        STA $C801             ; pitch
        LDA #$41              ; gate on + triangle
        STA $C804
```

Reference: `software/sid/Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm` (register definitions at the top, real-time keyboard-driven playback loop).

**From Applesoft**: `POKE &HC818,16 : POKE &HC805,8 : POKE &HC804,65`. Quick and dirty.

**Convert a C64 `.sid` tune**: [`tools/sid2apple1.py`](tools/sid2apple1.py) rewrites `$D400` → `$C800`, neutralises CIA/VIC touches, emits a `.bin` that loads at `$0280` — run with `280R`. Source tunes: [HVSC](https://www.exotica.org.uk/wiki/High_Voltage_SID_Collection).

---

## 8. Peripherals — the user-facing commands

The in-emulator Hardware Reference dialog (**Help > Hardware Reference**) is the authoritative source; this section is a quick-glance for agents. Covered commands match `MainWindow_Dialogs.cpp`.

### SD CARD OS (microSD, `8000R`)

| Command | Effect |
|---------|--------|
| `D` / `LS` | List current directory |
| `CD <dir>` / `CD ..` | **Only navigation primitive** — absolute `/PATH`, relative, or `..` |
| `PWD` | Print cwd (the prompt itself already shows it — `/PLAB/MCODE>`) |
| `LOAD <name>` | Fuzzy case-insensitive prefix match, reads at the tagged address |
| `SAVE` / `WRITE` | Write a memory range into the cwd |
| `DEL <name>` | Delete a file in the cwd |
| `MKDIR` / `RMDIR` | Create / remove a sub-dir in the cwd |

**Invariant**: every name-accepting command resolves against **`currentDirectory` only — no recursion**. Use `CD` to navigate before `LOAD`/`DEL`/`SAVE` on a file deeper in the tree. Regression-pinned by `tools/test_sdcard_subdir_navigation_telnet.py`.

**Tagged filename format**: `NAME#TTAAAA` where `TT` = type (`06` binary, `F1` Integer BASIC, `F8` Applesoft BASIC) and `AAAA` = hex load address. Example: `ACEYDUCEY#f10800` = Integer BASIC program loaded at `$0800`.

### Juke-Box Program Manager (`BD00R` — `&` prompt)

| Command | Effect |
|---------|--------|
| `H` | Help |
| `D` | List current page (32 kB EEPROM, up to 16 programs) |
| `L<X>` | Load program tagged letter `X` |
| `P<0-F>` | Page switch (multi-page 29c020 / 29c040 not yet modelled in POM1) |
| `B` | Drop into BASIC (via `E2B3R`, non-destructive) |
| `X` | Exit to Woz Monitor |

Sub-menu **Save Program** at `B800R` (`#` prompt): `W` write RAM range to EEPROM, `S` save current BASIC program, `L` back to Program Manager.

**Building a Juke-Box ROM**: use `doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py` — concatenates Program Manager + BASIC + your programs into a 32 KB image with signature `$A5` at file offset `$7D00` (= `BD00`). Install as `roms/jukebox.rom`, boot with preset #10. **Don't use P-LAB's `2-packer.sh`** — it produces subtly different layouts.

### MODEM BBS (`ATmodem` at `0280`, Hayes subset, desktop only)

`AT` / `ATDT host:port` / `ATH` / `ATE0` / `ATE1` / `ATI` / `ATZ`. `+++` (with 1 s guard) to switch back to command mode. Typical BBS flow:

```
<ATmodem loaded at $0280 from software/net/ATmodem.txt, Woz prompt>
0280R                         ; start the ACIA bridge
AT                            ; → OK
ATDT BBS.FOZZTEXX.COM:23      ; connect
... (talk to the BBS) ...
+++                           ; wait 1 s, escape
ATH                           ; hang up
```

### Terminal Card (TCP loopback `:6502`, desktop only)

- Default: 7-bit (CR→CRLF, uppercase-in via `Ctrl-I`, uppercase-out via `Ctrl-O`)
- `Ctrl-T` → 8-bit raw pass-through (needed for PETSCII / UTF-8 BBS output)
- `Ctrl-L` clear, `Ctrl-R` reset the Apple 1
- ESC-prefixed alternates (`ESC T/O/L/R/I`) for macOS/BSD ttys that eat the raw control chars before telnet sees them

### A1-IO & RTC (`$2000-$200F` VIA — mutually exclusive with GEN2 HGR framebuffer)

24-register broadcast pumped on a 100-cycle period with PORTB STROBE. Regs 0-5 = RTC (H/M/S/D/M/Y). Reg 6 = DS3231 die temperature. ADC + digital in/out follow. `software/a1io_rtc/` has a clock demo.

---

## 9. Deployment — how the program reaches the user

Four distribution channels, pick based on what the user asked:

1. **Memory load (default for dev iteration)** — ship `.apl.txt` or `.bin`, user does **File > Load Memory**, then `280R` (or whatever the start address is). Auto-enables the matching card if the file lives under `software/sid/`, `software/hgr/`, `software/tms9918/`, `software/net/`, or `sdcard/` — see `MainWindow_FileDialogs.cpp`.
2. **microSD tagged file** — drop `NAME#TTAAAA` into `sdcard/` (optionally under a sub-directory that users `CD` into first). Persistent across sessions, works in the WASM build too (preloaded into the MEMFS bundle).
3. **Juke-Box ROM bundle** — rebuild `roms/jukebox.rom` with your program baked in, ship alongside the emulator; user picks preset #10, types `BD00R`, picks the program from the `&` prompt.
4. **Cassette tape** — dump the capture to `.aci` / `.wav` / `.mp3` / `.ogg`, drop into `cassettes/`. Add a matching entry to `cassettes/tapeinfo.txt` (`filename = load-range`, e.g. `MYPROG.ogg = 0280.04FF`) so the deck's jaquette prints *"Type 0280.04FFR"* for the user. Works in both pulse mode (ACI plugged) and audio-stream mode (ACI unplugged, firmware-less playback).

**Applesoft programs**: `SAVE "NAME"` from the Applesoft prompt writes `sdcard/NAME#F80801` directly. No manual dump step.

---

## 10. Testing your program

### Manual path

1. Build (`ca65` + `ld65` + the `.bin` → `.txt` one-liner above).
2. Launch POM1 with the right preset: `./POM1 --preset 4 --terminal --cpu-max` (preset 4 = P-LAB microSD + Applesoft Lite, `--terminal` enables the `:6502` TCP bridge, `--cpu-max` pins the CPU at 1 MHz emulated so loads don't take 30 s of wallclock).
3. **File > Load Memory** → pick the `.txt`. Or use `--tape <path>` to auto-preload.
4. In the Woz Monitor, type `<start>R`.

### Automated path — telnet harness

The `tools/test_*_telnet.py` scripts drive the emulator via the Terminal Card. Two conventions in use:

- **Agent auto-launches POM1** (self-contained) — e.g. `tools/test_sdcard_subdir_navigation_telnet.py`, `tools/test_aci_telnet.py`. Usable for CI / regression pinning.
- **User runs POM1 separately** (manual setup). Script just connects to `127.0.0.1:6502` and drives.

**Skeleton for a new self-contained test**, copied directly from the existing pattern:

```python
import select, signal, socket, subprocess, time
from pathlib import Path

HOST, PORT, CTRL_R = "127.0.0.1", 6502, 18
REPO_ROOT = Path(__file__).resolve().parent.parent

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

def send_line(sock, cmd, wait=0.3, read_t=4.0):
    sock.sendall((cmd + "\r").encode("ascii")); time.sleep(wait)
    return recv_avail(sock, total=read_t, idle=0.4)

# 1. launch POM1
proc = subprocess.Popen(
    [str(REPO_ROOT / "build" / "POM1"), "--preset", "4", "--terminal", "--cpu-max"],
    stdout=open("/tmp/pom1.log", "w"), stderr=subprocess.STDOUT, start_new_session=True)
time.sleep(3.0)   # boot + 15-frame card defer

# 2. drive via telnet
sock = socket.create_connection((HOST, PORT), timeout=5)
recv_avail(sock, total=2.0)                            # drain banner
sock.sendall(bytes([CTRL_R])); time.sleep(0.9)         # soft reset to Wozmon
out = send_line(sock, "8000R", wait=0.6, read_t=3.0)   # enter SD CARD OS
assert "/>" in out or "prompt" in out.lower()

# 3. teardown
proc.send_signal(signal.SIGTERM); proc.wait(timeout=5)
```

Full working example: `tools/test_sdcard_subdir_navigation_telnet.py`.

### Test via the built-in ctest
For emulation-level invariants (CPU, bus, SID audio, ACI tape round-trip), add a C++ test in `tests/`. Template: `tests/peripheral_bus_smoke_test.cpp` — `<cassert>` + `add_test` in `tests/CMakeLists.txt`, no test framework needed.

---

## 11. Common gotchas — quick list

| Gotcha | Fix |
|---|---|
| Branch ±127 out of range | Invert the condition + `JMP` to the real target, or add a mid-routine trampoline label |
| `ADC` picks up stale carry | Put `CLC` before every *first* `ADC` of a sum |
| Helper uses `TAX` + `LDA tbl,X` and clobbers caller's X | Use `TAY` + `LDA tbl,Y` inside helpers — X is reserved for `STA arr,X` callers. See `feedback_6502_register_preservation.md` |
| Array > 256 bytes via `LDA arr,Y` | Use `(zp),Y` with a high-byte-adjusted pointer, or parallel lo/hi tables |
| `.include` path | Resolves relative to the *source file*, not the CWD — keep `.inc` beside the `.asm` |
| Lit HGR pixel renders coloured instead of white | Make sure every lit pixel has a lit neighbour. 1-px vertical line = colour; 2-px thick = white |
| TMS9918 shows random sprites at boot | Write `$D0` to the first sprite-Y byte (`$1B00`) to kill the sprite chain |
| Apple 1 text display "doesn't update" during a burst | `$D012` has a busy-wait delay — the CPU can outrun the display. Either poll bit 7 before writing, or just let it autopipeline |
| `$D012` writes echoing to the Terminal Card eat your CPU budget in telnet tests | Use `--cpu-max` and `Ctrl-T` (8-bit raw); the real TurboType fix is to install a Wozmon-free dropper that skips `$D012` altogether (see TODO `TurboType`) |
| Applesoft game has no grid because HOME/VTAB missing | Design around it — pure scroll-text games were the 1977 norm. Embrace the `$D012` write delay as pacing |
| microSD `LOAD YUM` says `FILE NOT FOUND` | YUM lives under `sdcard/PLAB/MCODE/` — `CD PLAB` then `CD MCODE` first. Prompt `/PLAB/MCODE>` tells you where you are |
| WOZ Monitor or SD CARD OS reloads twice on boot | Fixed — `Memory::loadApplesoftLiteSDCard` and `setMicroSDEnabled(true)` skip the redundant load via signature guards. Don't add new code paths that reload blindly |

---

## 12. Example-file index

When writing something new, start by copying a known-good example:

| Want to build… | Copy from… |
|---|---|
| Text-mode game with ASCII tiles | `software/games/Sokoban.asm` + `software/games/sokoban_common.inc` |
| Text-mode BASIC program | `software/basic/mini-startrek.apl.txt` (hex format), or write fresh Applesoft |
| HGR pixel plotter | `software/hgr/HGR4_Mandelbrot.asm` + `software/hgr/hgr_tables.inc` |
| HGR byte-aligned tile game | `software/hgr/HGR6_Sokoban.asm` (14-px-wide tiles) |
| HGR sub-byte tiles (≠ 7-px) | `software/hgr/HGR2_Maze.asm` (4-px walls) + `reference_subbyte_rendering.md` |
| TMS9918 game with multi-colour tiles | `software/tms9918/TMS_Sokoban.asm` (colour-group trick — 7 tile types × 8 chars) |
| TMS9918 full-screen board | `software/tms9918/TMS_Connect4.asm` (32×32 px pieces) |
| SID tune (direct register play) | `software/sid/Claudio_PARMIGIANI_SID_PIANO_AZERTY.asm` |
| SID tune (C64 conversion) | `python3 tools/sid2apple1.py Music.sid` |
| Shared ASM helpers across modes | `software/games/sokoban_common.inc` (pure-logic routines with mode-neutral API) |
| New linker config | `software/apple1.cfg` (3 328 B) or `software/apple1_4k.cfg` (4 096 B) or `software/hgr/apple1_gen2.cfg` (7 552 B, reserves HGR framebuffer) |

---

## 13. Checklist before declaring a program done

1. **Budget** — fits the chosen linker config? `ls -l build/*.bin` vs the config's CODE size.
2. **ZP usage** — all `.res`'d variables fit in `$0000-$0022` (or whatever the config reserves)?
3. **Uppercase input** — every `CMP #'?'` uses an uppercase letter?
4. **Keyboard layout prompt** — if the game uses WASD, did you add the QWERTY/AZERTY `1`/`2` prompt with `key_up_code` / `key_left_code` in ZP?
5. **Branch ranges** — any `BNE` / `BCC` / `BEQ` within ±127 bytes of its target? Assembler will catch it at `ca65` time.
6. **Win / loss condition** — do both intermediate states (e.g. *player on target*) get handled?
7. **Tested** — loaded via `File > Load Memory`, run once, watched at least one complete loop?
8. **Telnet test** — if the user will script it, does a minimal `test_*_telnet.py` pass?
9. **Artifact distribution** — decided which channel (memory load / microSD tagged / Juke-Box / cassette)?
10. **Docs** — added an entry to `README.md`'s *Software Library* tables if shipping it?
