# lib/sd — P-LAB microSD card byte-level handshake

*[← POM1 documentation index](../../../doc/README.md)*

Direct VIA-level access to the microSD MCU, bypassing the SD CARD OS
ROM at `$8000-$9FFF`. The ROM is fine for interactive use (Wozmon
prompt, `D`/`LOAD`/`CD` commands) but for utility programs that want
to integrate file I/O into a larger flow without tearing down the
prompt — `CAT` / `XCOPY` / `HEXDUMP` per TODO6502.md — you need to
talk to the MCU yourself.

This lib delivers the byte-level handshake. Higher-level command
framing (build a READ/WRITE/DIR sequence on top) is project-specific.

## Files

- **`sd.inc`** — VIA register addresses (`SD_VIA_PORTB`..`SD_VIA_IER`),
  strobe bit masks (`SD_CPU_STROBE`, `SD_MCU_STROBE`), MCU command IDs
  (`SD_CMD_READ`..`SD_CMD_MOUNT`), response codes. Pure equates,
  idempotent.
- **`sd.asm`** — `sd_via_init`, `sd_send_byte`, `sd_recv_byte`,
  `sd_send_str`.

## Public symbols

| Symbol | Type | Description |
|---|---|---|
| `SD_VIA_PORTB`..`_IER` | equate | 65C22 register addresses |
| `SD_CPU_STROBE`, `SD_MCU_STROBE` | equate | strobe bit masks |
| `SD_DDRB_INIT` | equate | DDRB value for the basic protocol |
| `SD_CMD_READ`..`SD_CMD_MOUNT` | equate | MCU command IDs (15 commands) |
| `SD_OK_RESPONSE`, `SD_ERR_RESPONSE` | equate | $00 / $FF return codes |
| `sd_via_init` | routine | one-time VIA setup |
| `sd_send_byte` | routine | A = byte → MCU |
| `sd_recv_byte` | routine | A = next byte from MCU |
| `sd_send_str` | routine | A=lo, X=hi → ASCIIZ ptr (NUL included in send) |
| `sd_str_lo`, `sd_str_hi` | ZP | pointer used by `sd_send_str` |

ZP usage: 2 bytes, declared with `.ifndef` so a ZP-tight project can
alias to existing pointer slots before the include.

## Half-duplex handshake protocol

PORTA ($A001) carries the data byte. PORTB ($A000) carries two strobe
lines: bit 0 from CPU, bit 7 from MCU. DDRA flips between $FF (CPU
sends) and $00 (CPU receives) per byte.

**Send** (CPU → MCU):
1. `STA $A001` — latch byte on PORTA.
2. `LDA #$FF / STA $A003` — DDRA = output (idempotent if already so).
3. `LDA #$01 / STA $A000` — raise CPU_STROBE.
4. Spin on `LDA $A000 / AND #$80 / BEQ` until MCU_STROBE = 1.
5. `LDA #$00 / STA $A000` — drop CPU_STROBE; handshake complete.

**Receive** (MCU → CPU):
1. `LDA #$00 / STA $A003` — DDRA = input.
2. `LDA #$01 / STA $A000` — raise CPU_STROBE (request byte).
3. Spin on `LDA $A000 / AND #$80 / BEQ` until MCU_STROBE = 1.
4. `LDA $A001` — read the byte.
5. `LDA #$00 / STA $A000` — drop CPU_STROBE.
6. `LDA #$FF / STA $A003` — restore DDRA = output (default).

Above this, command framing follows: send `SD_CMD_*` byte, then
arguments per the MCU spec, then receive response. See `sd.inc`'s
command table.

## Use — minimal LOAD

```asm
.include "apple1.inc"
.include "sd.inc"
.include "sd.asm"

main:
        JSR sd_via_init

        ; Reset the MCU state machine to a known IDLE phase
        LDA #SD_CMD_MOUNT
        JSR sd_send_byte
        JSR sd_recv_byte             ; expect SD_OK_RESPONSE

        ; LOAD <NAME>: send command, send NUL-terminated name, get
        ;   1-byte status, then file bytes (count is implicit — read
        ;   until SD_ERR_RESPONSE or use the tagged-filename load addr).
        LDA #SD_CMD_LOAD
        JSR sd_send_byte
        LDA #<filename
        LDX #>filename
        JSR sd_send_str              ; "MYFILE\0"
        JSR sd_recv_byte             ; status

        ; … receive file bytes via repeated sd_recv_byte …

        JMP WOZMON

filename:   .byte "MYFILE", 0
```

In your project Makefile:

    LIB := -I ../../lib/apple1 -I ../../lib/sd

## Required preset

microSD must be plugged. POM1:

- `--preset 8` — microSD + Applesoft Lite (most common)
- `--preset 10` / `9` — multiplexing fantasy (microSD + other cards)
- `--enable microsd` — adds it to any preset

(Juke-Box (`--enable jukebox`) is mutually exclusive with microSD — they share the
`$4000+` / `$8000+` ROM windows — so it does **not** carry the card.)

Note: the SD CARD OS ROM at `$8000-$9FFF` is loaded automatically when
the card is plugged. Your direct-handshake code needs to coexist —
either run before invoking `8000R`, or design the ROM out of your
load (use `--disable krusader` style if relevant).

## Performance + caveats

- **No timeout** in the spin loops. If the MCU is wedged or the card
  unplugged after init, `sd_send_byte`/`sd_recv_byte` hang forever.
  Add a counter-based guard if your project needs robustness against
  hot-unplug.
- **MOUNT first** (`SD_CMD_MOUNT` = 23) after any soft reset. The MCU
  state machine has phases (`IDLE`, `RECEIVING_STRING`, etc.) that
  don't auto-reset on CPU 6502 reset alone — the ROM does this on
  startup, your direct-protocol code must too.
- **CMD_TEST = 20** echoes back one byte. Useful for handshake
  validation in development.
- **DIR / LS** returns multiple entries terminated by a sentinel; the
  exact framing is best learned by reading the SD CARD OS ROM
  disassembly or `MicroSD.cpp:handleByteFromCPU`.

## What this lib doesn't do

- **Command framing** — no `sd_load(filename, addr)` wrapper. Each
  command's argument structure differs (some take a string, some a
  string + 2-byte length + N data bytes, some no args). Build the
  wrapper for the commands you actually need in your project.
- **Tagged filenames** — `NAME#TTAAAA` where TT = file type and AAAA =
  load address is a CONVENTION enforced by the file system, not the
  protocol. Construct the full name string yourself.
- **Path navigation** — `CD` / `MKDIR` / `PWD` change MCU state; your
  project tracks the current working directory if relevant.
