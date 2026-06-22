# applesoft_lite — Applesoft Lite (floating-point BASIC) for the Apple-1

The complete **Applesoft Lite** interpreter (Microsoft 6502 BASIC, stripped for
the Apple-1) packaged as a DevBench sketch. Open `applesoft-lite.s` in
*DevBench → POM1 Bench* and **Verify** — it assembles with the bundled cc65 just
like any other Apple-1 ASM sketch.

Source: <https://github.com/txgx42/applesoft-lite> (Tom Greene, 2008; disassembled
from the Apple II+ ROMs, S-C DocuMentor labels by Bob Sander-Cederlof). See
<https://cowgod.org/replica1/applesoft/>.

## Files

| File | Role |
|---|---|
| `applesoft-lite.s` | Main interpreter — open this in the Bench |
| `io.s` · `cffa1.s` · `wozmon.s` | Linked modules (`extraAsm`): Apple-1 I/O, CFFA1 LOAD/SAVE, Woz Monitor |
| `macros.s` · `zeropage.s` | `.include`d equates / macros |
| `applesoft_lite.cfg` | Linker config → contiguous `$E000-$FFFF` 8 KB ROM image |
| `.sketch.json` | DevBench build metadata (`cfg` + `extraAsm`) |

The sources are upstream verbatim except a single `.feature force_range` line near
the top of `applesoft-lite.s`, needed so modern ca65 (≥ 2.18) accepts the 2008
code's negative immediates (`lda #-9`) and `#<INPUTBUFFER-1` precedence. The
result is **byte-identical to the shipped `roms/applesoft-lite-cffa1.rom`**.

## Build by hand

```bash
ROOT=../../..                 # repo root, from this folder
ca65 -I . $ROOT/dev/lib/**/   applesoft-lite.s -o applesoft-lite.o   # (DevBench adds -I for every dev/lib dir)
ca65 -I . io.s -o io.o ; ca65 -I . cffa1.s -o cffa1.o ; ca65 -I . wozmon.s -o wozmon.o
ld65 -C applesoft_lite.cfg applesoft-lite.o io.o cffa1.o wozmon.o -o applesoft-lite.bin
```

## Running

This builds the canonical **CFFA1 flavour** — a full `$E000-$FFFF` ROM image
(BASIC `$E000-$FEFF`, Woz Monitor `$FF00`), cold start `E000R` / warm `E003R`. It
needs a machine that backs `$E000-$FEFF`; the plain "Apple-1 dual 4K/8K" Bench
preset only backs `$E000-$EFFF`. POM1 already loads this exact image at `$E000`
when the **CFFA1** card is plugged (`Memory::loadApplesoftLiteCFFA1`).

To just *use* floating-point BASIC interactively, the Bench's **BASIC language →
Applesoft Lite** target runs the same interpreter (relocated to `$6000`,
`roms/applesoft-lite-microsd.rom`) on the 64 KB Fantasy preset — no compile step.
