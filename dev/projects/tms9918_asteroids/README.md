# TMS_Asteroids — vector-style ship + bullet (V0.2)

Foundation milestone for an Asteroids-style game on the Apple-1 +
TMS9918. V0.2 has a single rotating ship sprite with classic Asteroids
physics (thrust adds velocity in the heading direction, no friction,
screen wraps on all four edges) plus a single in-flight bullet that
fires from the ship's tip and times out after ~1 second. No asteroids,
no collision yet — those land in V0.3+.

The point of V0.1 is to validate the
[`sprite_triangle.asm`](../../lib/tms9918/sprite_triangle.asm) pipeline
end to end on a real Apple-1 + TMS9918, and to set up the project
structure so V0.2 just slots more sprites into the existing game loop.

## Build

```bash
cd dev/projects/tms9918_asteroids
make
```

Output lands in `software/tms9918/TMS_Asteroids.{bin,txt}`. V0.1 weighs
about 2 kB out of the 15.5 kB CODE budget — plenty of headroom for the
next milestones.

## Run

```bash
./build/POM1 --preset 7       # P-LAB Apple-1 with TMS9918 + CodeTank
```

In Wozmon, paste the contents of `software/tms9918/TMS_Asteroids.txt`
and type `0280R`.

## Controls (AZERTY ZQSD, hold-to-act)

| Key   | Action                                                       |
|-------|--------------------------------------------------------------|
| `Q`   | Latch rotation left  (−3°/tick, ≈ 200°/s). No effect on thrust. |
| `D`   | Latch rotation right (+3°/tick).                              |
| `Z`   | Commit forward: stop rotation + engage thrust (latched).      |
| `S`   | Commit fire: stop rotation + stop thrust + retro impulse + bullet. |
| ESC   | Quit → Wozmon prompt                                          |

Apple-1 keyboards have no key-release event so `Q`/`D`/`Z` **latch**:
press once and the action runs every tick until you press an opposite
key (`Q` ↔ `D`) or commit (`Z` for thrust, `S` for fire). Same Galaga /
Snake pattern (`dev/projects/tms9918_galaga/TMS_Galaga.asm:4170`). Both
`Z` and `S` cancel rotation because pressing them means *"I want this
specific thing now, stop turning"*.

`S` is the multi-purpose **commit-fire**: it spawns a bullet, kicks the
ship slightly backward (retro impulse equal in magnitude to one thrust
tick), and pulls you out of any current rotation/thrust state. The
backward kick is enough to dodge the asteroid you just shot at.

Up to **4 bullets** may be in flight at once (classic Asteroids). Each
bullet has a ~0.85 s TTL **and** despawns instantly when it crosses any
screen edge — bullets do not wrap (the ship does).

## Architecture

The project links four modules:

| Module                     | Source                                          | Role |
|----------------------------|-------------------------------------------------|------|
| `TMS_Asteroids.asm`        | this dir                                        | game loop, ship state, input, motion integration, screen wrap |
| `math.asm`                 | `dev/lib/m6502/`                                | fixed-point trig, LFSR, mod360 |
| `tms9918m2.asm`            | `dev/lib/tms9918/`                              | Mode-2 bitmap driver (init + clear + sprite-disable) |
| `sprite_triangle.asm`      | `dev/lib/tms9918/`                              | rotating-triangle sprite rasterizer (computes 3 vertices in local sprite coords, Bresenham-rasterizes them into a 32 B RAM buffer, uploads to VRAM, places the sprite) |

The ship sits in a single 16×16 sprite slot (`tri_slot = 0`). Every
tick the game loop:

1. Applies `rot_dir`-driven rotation (`Q`/`D` set the state, `S` clears).
2. Applies thrust if `thrust_active` (`Z` sets, `S` clears).
3. Integrates `ship_v{x,y}` into a 16-bit `ship_{x,y}_int:frac`
   position, with sign-extending 8-bit add. Y wraps at 192, X wraps
   automatically at 256.
4. Calls `sprite_triangle_render` (rasterizes the triangle for the
   current `ship_angle`, uploads to sprite slot 0).
5. Updates the bullet (decrements TTL, advances pos, writes slot 1
   attribute or hides via `Y=$D0`).
6. `delay_and_input` — the throttle: SPEED × 256 inner iterations of
   "poll KBDCR, dispatch any pending key into the state machine".
   Ports the Galaga pattern (`dev/projects/tms9918_galaga/TMS_Galaga.asm:4170`)
   and bottlenecks the loop at ~70 ticks/s while staying responsive
   to input (~1500 polls per second).

`ship_dir_x` / `ship_dir_y` (signed sin / −cos × 64, exposed by
`sprite_triangle_render` as a side-effect) feed the thrust math: each
`/` tap shifts the direction vector right by 5 (= ÷32) and adds it to
`ship_v{x,y}`.

## Memory layout (`apple1_asteroids.cfg`)

```
$0000-$003F  ZP scalars (ZEROPAGE, 64 B)
$0100-$01FF  6502 stack
$0200-$027F  BSS (game state, fits in 128 B)
$0280-$3FFF  CODE: program code + RO data (~15.5 kB headroom)
$4000+       I/O / TMS9918 / cassette window — off-limits
```

No PROC segment (no proc storage like LOGO has) and no LBUF (no
command-line input). All BSS lives in the single $0200-$027F region.

## Roadmap

- **V0.2** ✅ single bullet on `S`. Worked but felt laggy: subsequent
  presses were ignored while a bullet was alive (~0.85 s).
- **V0.3** ✅ Galaga-style state-machine input (latched Q/D rotation,
  latched Z thrust, S commit-fire). Velocity clamp at signed-byte
  overflow stops the "ship bounces in vacuum" bug.
- **V0.4** ✅ multi-bullet pool (4 in flight max, slot 1..4 with shared
  pattern at $1820), bullets despawn at screen edges (no wrap), retro
  impulse on `S`, Z and S both cancel rotation.
- **V0.5** — asteroids. Three sizes (large 16×16, medium 12×12, small
  8×8). Each one gets its own sprite slot 5..N (after the chain
  terminator, which moves to follow the highest live slot). Drift in
  straight lines, wrap on screen edges. Spawn 4 large at level start.
- **V0.6** — collision. Bullet × asteroid splits the asteroid (large →
  2 medium, medium → 2 small, small → vapor + 100 pts).
  Asteroid × ship → game over.
- **V0.7** — score, lives, level transitions. HUD on the bitmap
  (score top-right, lives top-left). Re-uses `text_bitmap.asm` glyph
  blitter from the LOGO refactor.

## Sprite layout

| VRAM addr  | Slot | Use                                                  |
|------------|------|------------------------------------------------------|
| `$1800`    | pat 0 | ship triangle pattern (re-rasterized every frame)   |
| `$1820`    | pat 1 | bullet 4×4 dot (uploaded once at boot)              |
| `$3B00`    | attr 0 | ship attribute (written by sprite_triangle_render) |
| `$3B04..3B10` | attr 1..4 | bullets 0..3 attribute, name = 4 (shared pattern) |
| `$3B14`    | attr 5 | scan-chain terminator (`Y = $D0`, hard-stops chip)   |

Bullets share the pattern at slot 1 (sprite name = 4 in 16×16 mode).
Idle bullet slots get `Y = $C8` (off-screen but doesn't stop the scan
chain), so adding asteroids in V0.5 just needs to bump `SPRITE_TERMSLOT`
and reuse the same hide convention. The terminator at slot 5 prevents
random VRAM noise in slots 6..31 from ever rendering.

## Smoke verification

The binary loads cleanly under POM1 preset 7 and runs without hitting
BRK over a 3-second smoke window:

```bash
timeout 3 build/POM1 --preset 7 \
    --load 0280:software/tms9918/TMS_Asteroids.bin \
    --run 0280 \
    --snapshot-save /tmp/asteroids.snap
# exit 143 (SIGTERM from timeout = healthy), snapshot 64 KB written.
```

Visual verification (rotating triangle, screen wrap, thrust inertia)
needs an interactive POM1 session — there's no headless rendering test
for sprite output yet.
