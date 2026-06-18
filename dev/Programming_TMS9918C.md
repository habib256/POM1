# P-LAB TMS9918 Programming in C (cc65)

The cc65 build of Antonino "Nino" Porcino's `apple1-videocard-lib` lives
in-repo at `dev/lib/tms9918c/` and provides the `screen1` / `screen2` /
`tms9918` modules, plus sprite helpers, that drive the P-LAB Graphic Card
from a C program. It builds a 16 KB CodeTank ROM image that boots at `4000R`.

**Related docs**

| Doc | Use |
|-----|-----|
| [`Programming_C_Quickstart.md`](Programming_C_Quickstart.md) | **Beginner cheat sheet** — start here if you're new to cc65 on Apple-1 |
| [`_template_tms9918c/`](_template_tms9918c/) | Copy-paste starter project (sprites + shadow workflow) |
| [`Programming_Apple1_C.md`](Programming_Apple1_C.md) | Base cc65 / Apple-1 text + keyboard layer |
| [`Programming_TMS9918.md`](Programming_TMS9918.md) | 6502 assembly version of this guide, **mandatory reference for the silicon-timing model** |
| [`dev/lib/tms9918c/README.md`](lib/tms9918c/README.md) | `tms9918c` library reference, demos, build flags |
| [`doc/TMS9918-SPRITE_BEST_PRACTICES.md`](../doc/TMS9918-SPRITE_BEST_PRACTICES.md) | Operational sprite checklist |

The runtime hides most of the bus-timing detail: you don't have to write
`STA $CC00` loops yourself, you call `tms_copy_to_vram` and friends. But a
few silicon-handling decisions still **leak through** the C API and they
are documented in §3-§6 below. For everything else, cross-link to the ASM
guide rather than duplicate.

---

## 1. TMS9918 sprites and colour

Uses Nino Porcino's `apple1-videocard-lib` (`screen1.h`, `tms9918.h`,
`screen2.h`). The Bench's **TMS9918 (C)** target builds a 16 KB CodeTank
ROM and boots `4000R`. `dev/projects/tms9918c/` holds 7 directories —
`demo`, `demo_hello_world`, `demo_hello_screen1`, `demo_screen1`,
`demo_picshow`, `demo_sprite_animals`, and the `tool_checksum` utility —
so **6 worked demos** (the last entry is a tool, not a demo).

```c
#include "tms9918.h"
#include "screen1.h"
void main(void) {
    tms_init_regs(SCREEN1_TABLE);
    tms_set_color(COLOR_CYAN);   /* R7 = border colour in Graphics I; per-tile text
                                    colours come from the table screen1_prepare() sets */
    screen1_prepare();
    screen1_load_font();
    screen1_puts((const unsigned char *)"HELLO TMS9918");
    for (;;) { }
}
```

> **VRAM layout note.** This cc65 runtime uses the nippur72 table placement —
> name table `$3800`, sprite patterns `$1800`, SAT `$3B00`, colour `$2000` —
> which differs from the ASM playbook in `Programming_TMS9918.md` §2 (name
> `$1800`, SAT `$1B00`). The base addresses are register-configurable, so the two
> guides' memory maps are **not** interchangeable.

## 2. C-level entry points

The library is split across `tms9918.h` (VDP-level primitives) and
`screen1.h` / `screen2.h` (mode-specific text + bitmap helpers).

| Function | Module | Role |
|----------|--------|------|
| `tms_init_regs(table)` | `tms9918` | Program R0..R7 from an 8-byte init table |
| `tms_write_reg(n, v)` | `tms9918` | Write a single VDP register |
| `tms_set_vram_write_addr(a)` / `tms_set_vram_read_addr(a)` | `tms9918` | Latch the auto-increment VRAM cursor |
| `tms_copy_to_vram(src, size, dest)` / `tms_copy_to_vram_fast(...)` | `tms9918` | Bulk upload patterns / palettes |
| `tms_fill_vram(addr, val, count)` | `tms9918` | Clear or pattern-fill VRAM |
| `tms_set_color(c)` / `tms_set_blank(v)` / `tms_set_interrupt_bit(v)` / `tms_set_external_video(v)` | `tms9918` | Register-bit shortcuts |
| `tms_wait_end_of_frame()` | `tms9918` | Polled VBlank sync |
| `screen1_prepare()` / `screen1_load_font()` / `screen1_cls()` / `screen1_scroll_up()` | `screen1` | Graphics I init + font upload + clear |
| `screen1_putc(c)` / `screen1_puts(s)` / `screen1_putcharxy(x,y,c)` / `screen1_locate(x,y)` | `screen1` | Text I/O |
| `screen1_strinput(buf, max)` | `screen1` | Line-edited input |
| `screen1_fill_color_attr(fg_bg)` | `screen1` | Bulk colour-table update |

## 3. Silicon-handling that leaks through the C API

The runtime takes care of the slot-table model (Bug N°1 in
[`Programming_TMS9918.md` §17](Programming_TMS9918.md#bug-n1-vram-timing))
so you don't have to insert `JSR tms9918_pad12` by hand. But the
abstraction is **not airtight** — a few decisions remain on your side.

### `TMS_IO_DELAY()` between consecutive VRAM writes

`dev/lib/tms9918c/utils.h` exports:

```c
#define TMS_IO_DELAY() ((void)(*(volatile unsigned char *)0xCC01))
```

This is a deliberate dummy read of the control port. Cost: 4 CPU cycles
plus a bus access that goes through the chip's slot machine, which "stalls"
the loop on the chip's cadence. Use it whenever you write two VRAM values
in C closely enough that the cc65-generated code might compress them under
the silicon worst case (Graphic I + sprites: ~7.5 c worst-case slot gap;
see ASM guide §17 for the slot model). 

Side effect (also in the ASM guide §13): reading `$CC01` clears bits 5
(C), 6 (5S), and 7 (F) atomically and resets the control-port flip-flop.
**Avoid `TMS_IO_DELAY` when the code is monitoring those status bits**
— take a one-shot snapshot of status before the loop and test the
snapshot.

### Silicon-strict mode and dropped writes

When POM1's `--silicon-strict` is active (default outside the
*Multiplexing Fantasy* presets), bytes pushed faster than the slot table
allows are **silently dropped** by the emulator — exactly as on real
silicon. The C runtime's `tms_copy_to_vram_fast` and `tms_fill_vram`
paths (in `tms_fast.s`) are tuned for the upstream KickC cadence and stay
safe under strict mode. But hand-written loops that drive the data port
in a tight C `for` body can dip below the floor, especially with
`-O` enabled.

If your program shows corrupt VRAM under strict mode:

1. Reproduce in POM1 with `--silicon-strict` and capture the stderr
   diagnostic dump (Hardware menu → *Dump TMS9918 drop diagnostics*).
2. Locate the offending PC; cc65's assembler output (`.s`) maps it back
   to a C statement.
3. Insert `TMS_IO_DELAY()` between consecutive `*(volatile uint8_t *) 0xCC00
   = …` writes, or replace the hand-coded loop with one of the runtime
   helpers (`tms_copy_to_vram`, `tms_fill_vram`).
4. Retest. The Hardware menu's *Reset TMS9918 drop counter* clears the
   accumulator between runs.

See ASM guide §17 for the diagnostic infrastructure details and §25 for
the patcher used on the ASM side; the C runtime doesn't need the
`silicon_strict_patch.py` script because the macros are inline.

### Sprite-disable init step

Graphics I always enables sprites and the SAT contains random values at
power-on. The C runtime's `screen1_prepare()` performs the sprite-disable
step (writes `$D0` to the first SAT Y byte). **If you bypass
`screen1_prepare` and program the registers manually via `tms_init_regs`,
you must add the disable yourself** — see the ASM guide §10 for the
canonical sequence and §11 for the SAT layout that the disable plants
the terminator into.

### Polling vs IRQ choice

The runtime's `tms_wait_end_of_frame()` uses **polling** of bit 7 of the
status register — the recommended pattern (cf. ASM guide §18, Bug N°2).
This is independent of the I flag and stays portable across stock and
modified P-LAB cards.

If you want IRQ-driven sync, the P-LAB `/INT` line is wired to the 6502
`/IRQ` (verified by Parmigiani) and POM1 defaults to `irqStrapped = true`.
You'd then:

1. Install a handler at vector `$FFFE` (cc65: declare an assembly stub —
   the C runtime alone cannot place a vector).
2. Read `$CC01` **atomically as the first instruction** of the handler
   to reset the control-port flip-flop (ASM guide §19, Bug N°9).
3. Set R1 bit 5 = 1 via `tms_set_interrupt_bit(1)` and execute `CLI`
   from an inline assembly block.

Polling stays the recommended pattern for portable code — the C-level
`tms_wait_end_of_frame()` does it correctly and avoids the flip-flop
reentrancy pitfall entirely.

### 4K vs 16K mode (Bug N°3)

`tms_init_regs(SCREEN1_TABLE)` programmes a register table that already
sets R1 bit 7 = 1 (16K mode). If you build your own table via
`tms_write_reg(1, …)`, **always OR `$80`** — ASM guide §3 lists the
typical safe values. Forgetting it works on POM1 (which treats VRAM as
16 KB unconditionally) and breaks on silicon (the address truncates to
12 bits, all tables overlap).

### Collisions and the visible-window rule (Bug N°4)

The C runtime exposes the raw status byte; collision testing uses a
software bounding-box just like the ASM side. Don't expect the collision
bit to fire for sprites in the overscan zone — ASM guide §11 explains
the canonical-reference choice.

## 4. Per-frame status snapshot pattern

For games that need bit 5 (collision) or bit 6 (5S) **and** bit 7 (F) on
the same frame, snapshot the status byte once per frame:

```c
unsigned char vdp_status_snapshot;

void wait_frame(void) {
    /* Spin until VBlank flag arms. tms_wait_end_of_frame is also fine
       — it does this internally, but discards the snapshot. */
    while (((vdp_status_snapshot = *(volatile unsigned char *) 0xCC01) & 0x80) == 0) { }
}
```

After the call, `vdp_status_snapshot` holds bits 7/6/5 + the 5S index
(bits 0..4 — see ASM guide §13 for the "last sprite walked" semantics
of Bug N°6 and the requirement to check bit 6 before treating bits 0..4
as the 5S index).

## 5. When to drop down to assembly

The C runtime is pleasant for game state, AI, scoring, menus, and any
non-time-critical pixel pushing. Drop to inline assembly or a separate
`.s` file when:

- You need a tight VBlank-bounded SAT upload at 60 Hz (Galaga class) —
  use `tms_copy_to_vram_fast` or hand-craft the pad cadence per ASM
  guide §17.
- You're exploiting raster splits via the 5S flag (ASM guide §12, Bug
  N°10). The polling loop must be tight enough that bit 6 latches at
  the intended scanline; a cc65-compiled `while` loop adds enough
  overhead that the split lands on the wrong line.
- You want IRQ-driven sync (above). Install the handler in assembly so
  you control the first instruction (the `LDA $CC01` flip-flop reset of
  Bug N°9).

For everything that's hidden behind the runtime, see
[`Programming_TMS9918.md`](Programming_TMS9918.md) rather than
duplicate it here.

## 6. POM1 fidelity reference

The full fidelity table (per-bug status, legend) lives in the ASM guide
[§30 *POM1 fidelity status table*](Programming_TMS9918.md). All bugs are
🟢 SOLID except thermal drift of sprite cloning, which is out of scope
for every emulator.
