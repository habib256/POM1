# dev/lib/test — unit-level 6502 micro-tests

*[← dev/lib index](../README.md)*  ·  harness: [`../../../tools/test_lib_micro.py`](../../../tools/test_lib_micro.py)

Execution-level unit tests for the routines under `dev/lib/`. Unlike the
top-level suite (which validates libs **indirectly**, only when some
`dev/projects/` build happens to link them), each driver here loads **one** lib,
runs it headless inside POM1, and asserts **real values** read back from a RAM
"result mailbox" — no golden hashes, no rendering. Registered as the CMake test
`lib_micro_tests` (skips `77` without cc65 or `build/POM1`).

## How a run works

`tools/test_lib_micro.py` exploits POM1's headless CLI executing its verbs in
argv order:

```
POM1 --headless --preset N --silicon-strict \
     --load 0300:test.bin --run 0300 --step NSTEPS \
     --snapshot-save out.snap --dump-tms-frame scratch.png --dump-after-cycles 1000
```

The driver is assembled/compiled (`ca65`+`ld65` for `.s`, `cl65` for `.c`),
cold-started, single-stepped `NSTEPS` instructions, then a full machine snapshot
is saved. The harness parses the snapshot's `MEM` section and compares the
mailbox bytes against the `EXPECT:` lines in the driver header. The trailing
frame dump is just an exit ticket so the process terminates cleanly.

Every run also asserts **zero `[TMS9918 DROP`** lines on stderr — so each
micro-test doubles as a silicon-strict pacing check for the routine (catches
unpadded back-to-back VDP access; not marginal 9–16c pacing — see the caveat in
the harness docstring).

## The mailbox contract ([`micro/micro.cfg`](micro/micro.cfg))

```
$0000-$00FF  ZEROPAGE   driver + lib .exportzp slots
$0300-$0DFF  CODE+RODATA loaded via --load 0300, entered at 0300
$0E00-$0EFF  BSS        lib scratch (tri_*, shadowcast map/vis…)
$0F00-$0F7F  MAILBOX    never allocated by ld65; driver writes raw absolute
                        bytes here ($0F00 = magic $A5 written LAST). Keep BSS < $0F00.
```

## Driver header contract

Comment lines (`;` for `.s`, `*`/`//` for `.c`):

```
POM1-LIB-MICRO-TEST          required marker
LIBS:  path ...              lib sources, relative to dev/lib
CFG:   micro.cfg             linker cfg
PRESET: 1                    --preset index (default 1)
MODE:  codetank              .c only: build a 32 KB CodeTank ROM, boot 4000R
LOAD/RUN: 0300               .s load/entry address (default 0300)
STEPS: 120000                --step budget (driver spins at end)
EXPECT: ADDR B0 B1 ...       hex bytes expected at ADDR (repeatable)
```

## Current drivers ([`micro/`](micro/))

| Driver | Lib under test |
|---|---|
| `t01_m1_init_sat.s` | `tms9918/tms9918m1` — `init_vdp_g1` + `disable_sprites` |
| `t02_m1_rw_roundtrip.s` | `tms9918/tms9918m1` — `vdp_set_write` / `vdp_set_read` |
| `t03_pad18_transparency.s` | `tms9918/tms9918_pad` — `pad18` register+flag contract |
| `t04_lr_fill.s` | `basicrt/basicrt_tms` — `lr_fill` / `lr_setw` / `lr_setr` |
| `t05_sprite_attr_terminator.s` | `tms9918/sprite_triangle` — SAT `$D0` terminator dodge |
| `t06_math_vectors.s` | `m6502/math.asm` — shared-ZP arg/tmp window semantics |
| `t07_rand_mod.s` | `games/rogue/dungeon` — `rand_mod` + `prng16` |
| `t08_shadowcast.s` | `games/rogue/shadowcast` — FOV + index math |
| `t09_c_sprite_dodge.c` | `tms9918c` (C) — `tms_set_sprite` / SAT terminator |
| `t10_m1_upload_name_at.s` | `tms9918/tms9918m1` — `name_at_rc` + `vdp_upload_a` |

Each header's `GUARDS:` block names the exact bug class it pins (e.g. `t06` pins
the shared-ZP argument window whose clobber let a `Maze3D` bug survive for
months).

## Adding a test

Drop `tNN_<name>.{s,c}` in [`micro/`](micro/) with the header contract above,
write the routine's inputs, and store outputs at `$0F01+` (magic `$A5` at `$0F00`
written **last**). No CMake edit needed — the harness discovers every driver in
`micro/`. Keep any BSS below `$0F00`.

> **Sibling test**: [`../apple1/test/`](../apple1/test/) is a *structural*
> (link-time) pin, not an execution test — it runs nothing but makes `ld65`
> fail if the `apple1/zp.inc` slot pool drifts.
