# Programming the Apple 1 in C (cc65)

The C counterpart to [`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md).
You write C, the **cc65** cross-compiler (`cl65`) turns it into a 6502 binary,
and POM1 runs it. No prior 6502 knowledge required — but the Apple 1 is a tiny
machine, so a few rules below keep you out of trouble.

**Fastest path:** *DevBench → POM1 Bench*, **New sketch → Language: C**, pick a
machine, press **Upload**. The Bench wires the toolchain, the linker config and
the libraries for you. This guide is for when you want to understand or build by
hand.

**Brand-new to cc65 on Apple-1?** Read [`Programming_C_Quickstart.md`](Programming_C_Quickstart.md)
first — it's a one-page cheat sheet with three side-by-side hello-worlds,
function-chooser tables for each library, and the top-10 pitfalls.

**Contents:** [Install](#1-install-cc65) · [Architecture](#2-architecture--one-text-base-two-graphics-layers) · [Your first program](#3-your-first-program) · [Text & keyboard](#4-text--keyboard-apple1c) · [Graphics cards](#5-graphics-cards--separate-guides) · [Memory budget](#6-memory-budget--the-1-gotcha) · [Gotchas](#7-gotchas)

Card-specific guides: [`Programming_GEN2C.md`](Programming_GEN2C.md) (HGR) and [`Programming_TMS9918C.md`](Programming_TMS9918C.md).

---

## 1. Install cc65

| OS | Command |
|---|---|
| Debian / Ubuntu | `sudo apt install cc65` |
| Fedora | `sudo dnf install cc65` |
| Arch | `sudo pacman -S cc65` |
| macOS | `brew install cc65` |
| Windows / other | <https://cc65.github.io/> — download, add `bin/` to `PATH` |

Check it: `cl65 --version`. The POM1 Bench detects it automatically and tells you
how to install it if it's missing.

---

## 2. Architecture — one text base, two graphics layers

The key idea: **Apple-1 text + keyboard I/O is one shared library**, and each
graphics card adds its own drawing layer on top. Learn the text API once; reuse
it everywhere.

```
                 apple1c   (woz_puts / apple1_getkey / woz_mon — the WOZ terminal)
                /         \
   GEN2 HGR (gen2c)        TMS9918 (tms9918c: screen1 / tms9918)
   280x192 colour          256x192, 32 sprites
```

| You want… | Include | Linker cfg | Runs at |
|---|---|---|---|
| **Plain text** (40×24 terminal) | `apple1c.h` | `dev/cc65/apple1_c.cfg` | `0300R` |
| **GEN2 HGR colour** (+ text) | `gen2.h` (+ `apple1c.h`) | `dev/cc65/apple1_gen2_c.cfg` | `6000R` |
| **TMS9918 sprites/colour** | `tms9918c.h` | `dev/lib/tms9918c/cc65/codetank_c.cfg` | `4000R` |

Libraries:
- `dev/lib/apple1c/` — the shared text/keyboard base (`woz_puts` / `apple1_getkey` / `woz_mon`) ([README](../../dev/lib/apple1c/README.md)).
- `dev/lib/gen2c/` — GEN2 HGR runtime ([README](../../dev/lib/gen2c/README.md)).
- `dev/lib/tms9918c/` — Nino Porcino's TMS9918 runtime (vendored from nippur72/apple1-videocard-lib). Projects: `sketchs/tms9918/`.

---

## 3. Your first program

```c
#include "apple1c.h"          /* umbrella header (aliases apple1io.h) */

void main(void) {
    woz_puts((const unsigned char *)"\rHELLO WORLD\r");
    woz_mon();                 /* return to the '\' Monitor prompt */
}
```

Build and run (plain text):

```bash
cd dev/lib/apple1c
cl65 -t none -Oirs -C ../../cc65/apple1_c.cfg -I . \
     first.c apple1io.c apple1io_asm.s -o first.bin
# In POM1: File > Load Memory > first.bin, then 0300R
```

Two non-obvious rules you just used:
- **`void main(void)`**, not `int main()`. There's no OS to return a code to.
- **No `<stdio.h>` / `printf`.** You print with `woz_puts` / `woz_putc`. (cc65's
  full `printf` exists but pulls in a lot — keep it tiny.)

---

## 4. Text & keyboard (`apple1c`)

`#include "apple1c.h"` (the umbrella header; it pulls in the `apple1io.h`
implementation) — works on every machine.

| Function | Effect |
|---|---|
| `woz_putc(c)` / `woz_puts(s)` | print a char / a string |
| `woz_print_hex(b)` / `woz_print_hexword(w)` | print hex (no `printf` needed) |
| `woz_mon()` | return to the WOZ Monitor |
| `apple1_getkey()` | **block**, return `key & 0x7F` (uppercase only — the keyboard forces it) |
| `apple1_readkey()` | `0` if no key, else the key (non-blocking — game loops) |
| `apple1_iskeypressed()` | nonzero if a key is waiting |

```c
#include "apple1c.h"
void main(void) {
    unsigned char k;
    woz_puts((const unsigned char *)"\rPRESS A KEY: ");
    k = apple1_getkey();
    woz_putc(k);
    woz_mon();
}
```

---

## 5. Graphics cards — separate guides

**GEN2 HGR colour (`gen2c`)** is covered in [`Programming_GEN2C.md`](Programming_GEN2C.md).

**TMS9918 sprites & colour** is covered in [`Programming_TMS9918C.md`](Programming_TMS9918C.md). Before optimising VRAM loops, read its silicon-handling section §3 and [`Programming_TMS9918.md`](Programming_TMS9918.md) Part 5.

---

## 6. Memory budget — the #1 gotcha

`apple1_c.cfg` (plain text) gives C only **`$0300-$0DFF` ≈ 2.75 KB** for
code + data (the C stack sits above, `__STACKSTART__` = `$1000` growing down
through `$0E00-$0FFF`). A few hundred bytes of global arrays and the linker
overflows with a cryptic `ld65: Range error`. If you hit it:
- move work into smaller functions / fewer globals, **or**
- target **GEN2** (`apple1_gen2_c.cfg`, code at `$6000-$BEFF` ≈ 24 KB), **or**
- use a bigger preset (the Multiplexing Fantasy, `pom1_fantasy.cfg`).

| cfg | C code+data space | Notes |
|---|---|---|
| `apple1_c.cfg` | ~2.75 KB (`$0300-$0DFF`) | tight — small programs only (C stack at `$1000`↓) |
| `apple1_gen2_c.cfg` | ~24 KB (`$6000-$BEFF`) | roomy, above the HGR framebuffer |
| `codetank_c.cfg` | 16 KB ROM image (`$4000`) | TMS9918, runs from ROM |

---

## 7. Gotchas

| Gotcha | Fix |
|---|---|
| `int main()` misbehaves | use `void main(void)` |
| `printf` not found / huge | use `woz_puts` / `woz_print_hex` / `gen2_hgr_putu` |
| `ld65: Range error` on a small program | out of RAM — see §7, pick a roomier target |
| GEN2 clear is slow + screen goes black for seconds | use `gen2_hgr_clear()`, not a 16-bit `for` loop |
| GEN2 colours look wrong / switches don't stick | never `STA $C25x`; the macros *read* to toggle |
| Keyboard compare fails for lowercase | the keyboard is **uppercase only** — compare `'A'`, never `'a'` |
| C is missing in the web build | the cc65 Bench is **desktop-only**; compile on desktop |

See also: [`APPLE1DEV.md`](APPLE1DEV.md) §1 decision tree, §3 I/O cheat sheet;
[`Programming_Apple1_ASM.md`](Programming_Apple1_ASM.md) for the assembly route.
