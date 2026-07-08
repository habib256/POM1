# tms9918c — cc65 port of apple1-videocard-lib (CodeTank only)

*[← dev/lib index](../README.md)* · asm sibling: [`../tms9918/`](../tms9918/)

*Tutorials: [TMS9918 C programming guide](../../../sketchs/doc/Programming_TMS9918C.md) · [TMS9918 sprite init](../../../sketchs/doc/TMS9918-SPRITE_INIT.md).*

C **cc65** port of the original **[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)** library by **Antonino "Nino" Porcino** (KickC). Every improvement under this tree preserves the upstream attribution (header in every `.c` / `.h` / `.s`) — see [License / attribution](#licence--attribution).

POM1 target: **P-LAB CodeTank**, **16 KB ROM image @ `$4000-$7FFF`**, boot from **Wozmon `4000R`**, preset **9** (TMS9918 + CodeTank).

## Memory map (linker `dev/lib/tms9918c/cc65/codetank_c.cfg`)

| Region    | Address        | Role |
|-----------|----------------|------|
| ZP        | `$0000-$00FF`  | cc65 ZEROPAGE |
| Low RAM   | `$0280-$0FFF`  | `.data` (run), C stack (`__STACKSTART__` = `$1000`) |
| High RAM  | `$E000-$EFFF`  | `.bss` — high dual-bank bank (the "BASIC area" stays free as long as BASIC is not mapped there) |
| ROM       | `$4000-$7FFF`  | `CODE`, `RODATA`, load image for `.data` |

No `$0280`-only program variant; no Fantasy target.

## Requirements

- [cc65](https://cc65.github.io/) (`cl65`, `ca65`, `ld65`) on the `PATH`.

## Build

Each demo carries its own `Makefile`. Build one:

```bash
make -C sketchs/tms9918/demo_hello_world
```

Or build every project at once via the CI gate (it globs `*/*/Makefile`, so all
demos below are covered):

```bash
make -C dev/codetank
```

Outputs (per demo): `software/Apple-1_TMS_CC65/<name>.{bin,txt}` — 16 KB image (`$FF` padding), Wozmon hex + `4000R`.

> The seven C projects below share this single README (no per-project README);
> each demo's `main.c` header documents its own specifics.

| Demo (cc65) | Role |
|-------------|------|
| `demo_hello_world` | Wozmon "hello" message, no TMS (port of upstream `demos/hello-world`) |
| `demo_hello_screen1` | Minimal CodeTank + TMS text mode (Screen 1) |
| `demo_screen1` | Screen 1 text + reverse + charset + sprites + input line (port of `demos/demo/demo_screen1.h`) |
| `nino-democ` | Minimal menu: `SCREEN1` / `SCREEN2` (other keys print "not ported") |
| `demo_picshow` | Screen 2 geometry demo (text + circle + ellipse). The full upstream `picshow` image does not fit the 16 KB CodeTank ROM, so this ships a geometric stand-in. |
| `demo_sprite_animals` | Four fixed 16×16 Fauna sprites at native size, from `dev/lib/tms9918/sprites_fauna.asm` (SCROLL-O-SPRITES "Fauna", CC-BY Quale) |
| `tool_checksum` | Byte sum over a hex range, Wozmon (port of `demos/checksum`) |

Not ported here (KickC / big `.c` / other hardware): `anagram`, `tapemon`, `sdcard`, `montyr`, `life-src`, `iec`, `viatimer` (the upstream repo remains the reference for these demos).

## Testing under POM1

1. Preset **9** (Apple-1 + TMS9918 + CodeTank).
2. **File → Load Memory** from `software/Apple-1_TMS_CC65/` (TMS9918 auto-plugs), or paste the `.txt`, then **`4000R`**.

## Modules `lib/`

**Not monolithic — per-family objects, same dead-strip story as `gen2c`.**
`tms9918c.mk` exposes per-family source sets (`TMS9918C_CORE_SRCS`,
`TMS9918C_SCREEN1_SRCS`, `TMS9918C_SCREEN2_*_SRCS`, `TMS9918C_SPRITES_SRCS`,
`TMS9918C_VSYNC_SRCS`, `TMS9918C_PRINTLIB_SRCS`, `TMS9918C_RANDOM_SRCS`,
`TMS9918C_APPLE1_SRCS`, …) so a program lists only the families it calls;
`ld65` strips at `.o` granularity, not per-function, so an unlisted family
costs zero ROM. `TMS9918C_ALL_SRCS` links the lot (what the historical demos
do). For the Model-A/Model-B split and why object-level granularity matters,
see [`../README.md`](../README.md).

### Base (direct upstream port)

| File           | Role |
|----------------|------|
| `utils.h`      | `byte` / `word` types, `PEEK` / `POKE`, TMS I/O delay |
| `tms9918.*`    | Registers / VRAM (`$CC00` / `$CC01`) |
| `apple1.*` + `apple1_asm.s` | Wozmon ECHO / PRBYTE / keyboard |
| `screen1.*`    | TMS text mode (screen 1) |
| `screen2_*` (+ `screen2.h`) | Bitmap (screen 2), split per feature for ld65 dead-strip: `screen2_init.c` (bitmap setup), `screen2_pixel.c` (plot), `screen2_geom.c` (line / circle / `screen2_ellipse_rect` — 64-chord parametric in **C**, no `screen2_ellipse.s`), `screen2_text.c`, `screen2_ext.c` |
| `sprites.*`    | Sprite attributes (direct VRAM write) |
| `interrupt.*`  | `install_interrupt` / `wait_interrupt` stubs (no wired TMS IRQ in this port; the upstream dead counters were removed) |
| `c64font.c`    | 8×8 font (768 bytes) derived from upstream |

### POM1 extensions (beyond upstream)

These modules are port-specific additions; the code stays faithful to Nino's spirit (KickC) but leaves the upstream tree. Each is opt-in via the demo `Makefile`'s `SOURCES`, so the historical demos keep compiling unchanged.

| File                | Role |
|---------------------|------|
| `tms_fast.s`        | **ca65 VRAM fast-paths** — `tms_fill_vram(addr,val,count)`, `tms_copy_to_vram_fast(src,size,dest)`, `tms_shadow_flush()`. No per-byte `TMS_IO_DELAY` (upstream KickC cadence). |
| `sprite_shadow.*`   | **SAT shadow pattern** — a 128-byte `tms_sprite_shadow[]` mirror of the 32×4 Sprite Attribute Table held in RAM; mutate it freely (no VRAM-lock contention), `tms_shadow_set/move/clear/set_terminator`, then burst-flush all 128 B to VRAM `$3B00` inside VBlank via `tms_shadow_flush` (no mid-frame tearing; the `$D0` terminator never flickers through a transient VRAM value). |
| `random.*`          | 8-bit LFSR (period 255) + 16-bit Galois (period 65535) — `rand8`, `rand16`, `srand8/16`, `rand8_below(limit)`. |
| `vsync.*`           | Polling frame counter (`tms_wait_end_of_frame` → `vsync_frames`) — ~60 Hz NTSC time base in the absence of a wired TMS IRQ. |
| `printlib.*`        | Decimal / hex helpers via `putc` function pointer; Wozmon wrappers (`woz_print_dec_u8/u16`, `woz_print_hex_u16`) and screen 1 (`screen1_print_*`). |
| `screen1_ext.c` / `screen2_ext.c` | Optional extended helpers, split per card so a Screen-1-only program no longer drags in the Screen-2 bitmap helpers: `screen1_putcharxy(x,y,c)`, `screen1_fill_color_attr(c)` (screen1_ext); `screen2_clear()`, `screen2_filled_rect(x0,y0,x1,y1)` (screen2_ext, pulls `tms_fast.s`). |

### Example opt-in in a demo `Makefile`

```make
SOURCES := main.c \
    $(LIBDIR)/apple1_asm.s \
    $(LIBDIR)/tms9918.c \
    $(LIBDIR)/sprites.c \
    $(LIBDIR)/tms_fast.s \
    $(LIBDIR)/sprite_shadow.c \
    $(LIBDIR)/vsync.c \
    $(LIBDIR)/random.c
```

Then in `main.c`:

```c
#include "apple1_videocard_lib.h"   /* umbrella: pulls every module */

void main(void) {
    tms_init_regs(SCREEN1_TABLE);
    screen1_prepare();
    screen1_load_font();
    tms_shadow_init();
    srand16(0xACE1U);

    for (;;) {
        unsigned char i;
        for (i = 0; i < 4; ++i) {
            tms_sprite s;
            s.y = (signed char)(20 + (rand8() & 0x3F));
            s.x = (unsigned char)(rand8());
            s.name = (unsigned char)(i * 4);
            s.color = (unsigned char)(1U + (i & 0xEU)); /* 1..15 — never 0 =
                                     transparent (invisible sprite). For a
                                     deliberate -32 px X shift use
                                     `| EARLY_CLOCK` ($80 — sprites.h),
                                     NOT $10 (an undefined SAT bit). */
            tms_shadow_set(i, &s);
        }
        tms_shadow_set_terminator(4);
        vsync_wait();        /* wait for end-of-frame */
        tms_shadow_flush();  /* 128 B to VRAM in one burst, no tearing */
    }
}
```

## License / attribution

Code **derived** from the **[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)** repository by **Antonino "Nino" Porcino** (aka *nippur72* on GitHub). To my knowledge the upstream repo **declares no license** (checked 2026-05): consequently any redistribution outside the POM1 tree requires the author's agreement. Every `.c` / `.h` / `.s` in `lib/` carries an attribution header — **do not remove it** in forks. The extension modules above are POM1 contributions and keep the same notice out of respect for the derived chain.

## KickC → cc65 deltas

- No KickC pragmas, no `asm { }` blocks: sensitive routines live in **`apple1_asm.s`** (ca65).
- No `static inline` in cc65: `tms_read_status` → macro.
- `screen2_ellipse_rect`: **C** — parametric approximation (64 chords); requires **screen 2 mode** (`tms_init_regs(SCREEN2_TABLE)` + `screen2_init_bitmap`…).
- `install_interrupt`: stub — does not touch the reset zone `$0000` on a real Apple-1.

## Source of truth (asm ↔ C)

The VDP port addresses in `tms9918.c` (`VDP_DATA` = `$CC00`, `VDP_REG` = `$CC01`)
**mirror** the canonical asm equates in [`../tms9918/tms9918.inc`](../tms9918/tms9918.inc)
(`VDP_DATA`, `VDP_CTRL`). Edit the `.inc` first; this port follows. Pinned by
`tools/check_lib_equates.py` (`make -C dev/lib check`).

Note: this lib also ships its **own** Wozmon I/O (`apple1.c` / `apple1_asm.s`),
distinct from [`../apple1c/`](../apple1c/) — the upstream nippur72 port predates
the shared base and was kept intact to preserve attribution. New C cards should
build on `apple1c` instead.
