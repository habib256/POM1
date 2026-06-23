# dev/cc65 — linker configs & build layer

*[← dev/ index](../README.md)* · libraries: [`../lib/`](../lib/README.md) · projects: [`../projects/codetank/`](../projects/codetank/README.md)

The cc65 build plumbing every `dev/` program shares: `ld65` memory-layout
`.cfg` files, the `Makefile.common` fragment that drives `ca65`+`ld65`, the
space-escaping helper `space.mk`, and the Woz-hex `.txt` emitters. A project's
own Makefile sets a handful of variables and `include`s this directory; the
final `.bin` (+ optional `.txt`) lands under `software/<dir>/` — what POM1
loads.

## Linker configs (`.cfg`)

A `.cfg` tells `ld65` the target's **memory layout** (RAM/ROM windows), the
program's **load address**, and which segments go where. Pick the one that
matches the POM1 machine + RAM the program targets. All configs reserve
`$0100-$01FF` for the 6502 stack and `$0200-$027F` for Wozmon's keyboard input
buffer; the differences are the code window and ZP size.

| `.cfg` | Track | Load / run | Code window | Target |
|---|---|---|---|---|
| `apple1_4k.cfg` | asm | `$0280` (`280R`) | `$0280-$127F` (4 KB) | Text-mode + TMS9918 (VRAM is off-bus, so no HGR RAM needed). 35 B ZP. The default for plain programs. |
| `apple1_c.cfg` | C (cc65) | `$0300` (`0300R`) | `$0300-$0FFF` + stack to `$1000` | Plain text-mode C on the 8 KB dual-bank machine (RAM `$0000-$0FFF` + `$E000-$EFFF`). Full 256 B ZP, cc65 CONDES/startup segments. |
| `apple1_gen2.cfg` | asm | `$E000` (`E000R`) | `$E000-$EFFF` (4 KB) | GEN2 HGR, ≤ 4 KB single-bank. Code in the high bank (where Integer BASIC ROM used to sit); GEN2 framebuffer reserved at `$2000-$3FFF`. 64 B ZP. |
| `apple1_gen2_c.cfg` | C (cc65) | `$6000` (`6000R`) | `$6000-$BEFF` | GEN2 HGR C, 48 KB machine (preset 11). Code lives **above** the HGR pages (`$2000`/`$4000`) and text/lores (`$0400`/`$0800`). |
| `codetank.cfg` | asm | `$4000` (`4000R`) | `$4000-$7FFF` (16 KB) | Standalone CodeTank ROM card — code runs in place from the ROM window; only writes need RAM (ZP / `$0280-$0FFF`). RODATA folded into CODE so tables serve straight from ROM. |
| `pom1_fantasy.cfg` | asm | `$0300` (`300R`) | `$0300-$82FF` (32 KB) | XXL programs on the 64 KB Multiplexing Fantasy presets (10 / 12). **Not real-Apple-1 portable.** Adds a separate `BASIC_RAM` BSS region `$A000-$BFFF` (8 KB, not in the `.bin` — program zeroes it). Requires Applesoft Lite + SD CARD OS + GEN2 unplugged. |

**Project-local variants.** When a program needs a different code start/size —
a CodeTank menu-bank slot (`$5E00`, `$7100`, …), the 8 KB-jumper lower-half
window, or a split-bank `file = "%O.lo"`/`"%O.hi"` pair — keep the tweaked
`.cfg` *in the project directory*, not here. This directory stays the canonical
set. Examples in-tree: `dev/projects/codetank/bank_cfgs/*.cfg` (per-game bank
slots) and `sketchs/gen2/game_sokoban/apple1_sok_hgr.cfg` (the canonical
split-bank `.lo`/`.hi` layout — note that mixing a split-file region and a
single-file region in one cfg makes `ld65` emit one binary with an enormous gap,
so split builds need their own cfg + the `emit_dualbank.py` stitcher below).

### Which cfg?

| You're building… | Use |
|---|---|
| Text-mode asm (also TMS9918) | `apple1_4k.cfg` |
| Text-mode C | `apple1_c.cfg` |
| GEN2 HGR asm, fits 4 KB | `apple1_gen2.cfg` |
| GEN2 HGR asm, needs > 4 KB | project-local split-bank cfg (`.lo`/`.hi`) |
| GEN2 HGR C | `apple1_gen2_c.cfg` |
| CodeTank cartridge ROM | `codetank.cfg` (or a `bank_cfgs/` slot variant) |
| 32 KB+ program, Fantasy-only | `pom1_fantasy.cfg` |

## Makefile.common — the shared build target

A project Makefile defines the variables, then `include ../../cc65/Makefile.common`:

| Var | Meaning |
|---|---|
| `PROJECT` | basename → `<PROJECT>.asm` + `<PROJECT>.o` |
| `LOAD_CFG` | path to the `ld65` `.cfg` (one of the above, or a project-local copy) |
| `OUT_DIR` | destination under `software/` (raw spaces OK — `space.mk` escapes them) |
| `LIB` | `ca65 -I` include flags, e.g. `-I ../../lib/apple1 -I ../../lib/gen2` |
| `EXTRA_ASM` | *(optional)* extra `.asm` files assembled to their own `.o` and put on the link line — the Model-B path for heavy library objects (e.g. `../../lib/tms9918/tms9918m1.asm`) |
| `EMIT_TXT` | *(optional)* set to `1` to also emit Woz-hex via `emit_<PROJECT>_txt.py` |
| `CFG` | *(optional)* override knob — `CFG ?= default.cfg` + `LOAD_CFG := $(CFG)` enables `make CFG=variant.cfg` |

It builds `$(OUT_DIR)/$(PROJECT).bin` (`ca65 $(LIB)` then `ld65 -C $(LOAD_CFG)
$(OBJ) $(EXTRA_O)`), guards against a missing `ca65` with an actionable install
hint (override `CC65_BIN` to point at an out-of-PATH install), and exposes
`all` / `clean` / `test`. The `test` target runs `$(TEST_CMD)` (default: a
no-op stub); a project opts into a smoke test by setting `TEST_CMD` to a
`tools/test_*_telnet.py` invocation. Multi-target Makefiles (several `.asm`
targets) keep their own pattern rules rather than including this fragment.

Minimal text-mode project Makefile:

    PROJECT  := Hello
    LOAD_CFG := ../../cc65/apple1_4k.cfg
    OUT_DIR  := ../../software/Apple-1 demos
    LIB      := -I ../../lib/apple1
    include ../../cc65/Makefile.common

`space.mk` (auto-included by `Makefile.common`) backslash-escapes spaces in
`$(OUT_DIR)` for Make's tokeniser while keeping the unescaped, quoted form for
shell recipes — needed because `software/` directory names contain spaces
("Apple-1 games", "Graphic HGR").

## Emit scripts — flat binary → Woz-hex `.txt`

The `.txt` is what `File → Load Memory` parses: lines of `AAAA: BB BB …`
(8 bytes/line) closed by a `<START>R` run line so Wozmon auto-runs the program
on load. Three emitters cover the layouts:

- **`emit_woz.py`** — the general emitter. Usable as a module (a project's
  `emit_<PROJECT>_txt.py` becomes a ~5-line caller of `emit(asm_files=…,
  lib_dirs=…, cfg=…, out_dir_software=…, start_addr=…)`) or as a one-shot CLI
  (`--asm`/`--lib`/`--cfg`/`--out-software`/`--start`). It runs `ca65`+`ld65`
  itself (intermediates to `build/`), then writes the hex. `start_addr` **must
  match the cfg's load address** (`0xE000` for GEN2, etc.).
- **`emit_dualbank.py`** — stitches a split build (`.bin.lo` @ `$0280` +
  `.bin.hi` @ `$E000`) into one `.txt`; Wozmon's `AAAA:` address prefix spans
  the gap between the two blocks. Used by the Parmigiani 8 KB dual-bank
  projects (Chess trilogy, HGR Sokoban).
- **`emit_codetank_txt.py`** — trivial flat-binary → Woz-hex for a CodeTank
  image loaded at `$4000` (`--bin`/`--txt`/`--start`).

The multi-bank CodeTank *cartridge* ROM image (several games packed into one
28c256) is a separate concern — it's assembled by `tools/build_codetank_rom.py`,
not by a per-project link here.
