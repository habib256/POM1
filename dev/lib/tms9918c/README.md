# tms9918c — cc65 port of apple1-videocard-lib (CodeTank only)

*[← POM1 documentation index](../../doc/README.md)*

C **cc65** port of the original **[nippur72/apple1-videocard-lib](https://github.com/nippur72/apple1-videocard-lib)** library by **Antonino "Nino" Porcino** (KickC). Every improvement under this tree preserves the upstream attribution (header in every `.c` / `.h` / `.s`) — see [License / attribution](#licence--attribution).

POM1 target: **P-LAB CodeTank**, **16 KB ROM image @ `$4000-$7FFF`**, boot from **Wozmon `4000R`**, preset **7** (TMS9918 + CodeTank).

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

## Build — all demos

```bash
cd dev/projects/tms9918c
make -j         # builds all demo + tool subprojects
```

Or a single demo:

```bash
cd dev/projects/tms9918c/demo_hello_screen1
make
```

Outputs (per demo): `software/Apple-1_TMS_CC65/<name>.{bin,txt}` — 16 KB image (`$FF` padding), Wozmon hex + `4000R`.

| Demo (cc65) | Upstream source | Role |
|-------------|-----------------|------|
| `hello_screen1` | equivalent of `hello-world` + TMS | TMS text mode (screen 1) |
| `hello_world` | `demos/hello-world` | Wozmon message, no TMS |
| `checksum` | `demos/checksum` | Byte sum over a hex range |
| `graphs` | `demos/graphs` | Screen 2 bitmap: circle + ellipse |
| `demo_screen1` | `demos/demo/demo_screen1.h` | Text demo + reverse + sprites + input |
| `picshow` | `demos/picshow` | Stub variant (Screen 2 + drawing, not the large upstream image) |
| `demo` | `demos/demo` | Minimal menu: `SCREEN1` / `SCREEN2` (other options are stubs) |
| `tetris` | `demos/tetris` | Screen-1 Tetris (full game) |
| `text_adventure` | (inspired by Little Tower) | 32-column text adventure |
| `sprite_animals` | `dev/lib/tms9918/sprites_fauna.asm` | 4 static 16×16 Fauna sprites at native size (no MAG×2) |
| `chrome_dino` | (offline T-Rex clone) | Mini-game: jump + obstacles (part of the `make all` target) |
| `frogger_codetank` | (inspired by Frogger) | Hardware-sprite frog, animated water — local `make` |
| `rogue_c` | `dev/projects/tms9918/game_rogue/` | Partial C port of TMS_Rogue — local `make` (see `demos/rogue_c/README.md`) |

Not ported here (KickC / big `.c` / other hardware): `anagram`, `tapemon`, `sdcard`, `montyr`, `life-src`, `iec`, `viatimer` (the upstream repo remains the reference for these demos).

## Testing under POM1

1. Preset **7** (Apple-1 + TMS9918 + CodeTank).
2. **File → Load Memory** from `software/Apple-1_TMS_CC65/` (TMS9918 auto-plugs), or paste the `.txt`, then **`4000R`**.

## Modules `lib/`

### Base (direct upstream port)

| File           | Role |
|----------------|------|
| `utils.h`      | `byte` / `word` types, `PEEK` / `POKE`, TMS I/O delay |
| `tms9918.*`    | Registers / VRAM (`$CC00` / `$CC01`) |
| `apple1.*` + `apple1_asm.s` | Wozmon ECHO / PRBYTE / keyboard |
| `screen1.*`    | TMS text mode (screen 1) |
| `screen2.*`    | Bitmap (screen 2); `screen2_ellipse_rect` in **C** (64 segments, cos/sin tables, segments via `screen2_line`) — no `screen2_ellipse.s` file required |
| `sprites.*`    | Sprite attributes (direct VRAM write) |
| `interrupt.*`  | Stubs (no wired TMS IRQ in this port) |
| `via.*`        | VIA `$A000` symbols (microSD — unchanged vs upstream) |
| `c64font.c`    | 8×8 font (768 bytes) derived from upstream |

### POM1 extensions (beyond upstream)

These modules are port-specific additions; the code stays faithful to Nino's spirit (KickC) but leaves the upstream tree. Each is opt-in via the demo `Makefile`'s `SOURCES`, so the historical demos keep compiling unchanged.

| File                | Role |
|---------------------|------|
| `tms_fast.s`        | **ca65 VRAM fast-paths** — `tms_fill_vram(addr,val,count)`, `tms_copy_to_vram_fast(src,size,dest)`, `tms_shadow_flush()`. No per-byte `TMS_IO_DELAY` (upstream KickC cadence). |
| `sprite_shadow.*`   | **SAT shadow pattern** (cf. `doc/TMS9918-SPRITE_INIT.md §3.2 / §6) — 128-byte `tms_sprite_shadow[]` in RAM, `tms_shadow_set/move/clear/set_terminator` API, burst flush at VBlank via `tms_shadow_flush`. |
| `random.*`          | 8-bit LFSR (period 255) + 16-bit Galois (period 65535) — `rand8`, `rand16`, `srand8/16`, `rand8_below(limit)`. |
| `vsync.*`           | Polling frame counter (`tms_wait_end_of_frame` → `vsync_frames`) — ~60 Hz NTSC time base in the absence of a wired TMS IRQ. |
| `printlib.*`        | Decimal / hex helpers via `putc` function pointer; Wozmon wrappers (`woz_print_dec_u8/u16`, `woz_print_hex_u16`) and screen 1 (`screen1_print_*`). |
| `screen_ext.c`      | Optional extended helpers: `screen1_putcharxy(x,y,c)`, `screen1_fill_color_attr(c)`, `screen2_clear()`, `screen2_filled_rect(x0,y0,x1,y1)`. The last two pull in `tms_fast.s`. |

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
            s.color = (unsigned char)((i & 0xF) | 0x10); /* EARLY_CLOCK */
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
