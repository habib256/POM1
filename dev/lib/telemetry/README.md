# lib/telemetry — POM1 dev telemetry side channel (`$C440-$C443`)

*[← dev/lib index](../README.md)*

Helpers to drive POM1's **dev-only** telemetry side channel: a virtual register
window at `$C440-$C443` (not real hardware) that lets a 6502 program stream its
per-frame state to an external test harness over TCP, and — in lock-step — block
until the harness ACKs to advance exactly one frame. This turns a game into a
deterministic, scriptable fixture (the Snake / Sokoban smoke harnesses use it).

This README is the 6502-side reference; the register window, wire format and
control protocol below are complete on their own. The host-side decoder is the
Python harness `tools/pom1_telemetry.py`.

## Files

| File | Track | Role |
|---|---|---|
| `telemetry.inc` | asm (`.include`) | register equates + opcodes/type codes + the `TELE_*` macros (`TELE_ARM`, `TELE_PUT`, `TELE_FRAME`, `TELE_FIELD`, `TELE_SCHEMA_FRAME`) |
| `telemetry.h`   | C (cc65) | the same window as function-like macros (`tele_put`, `tele_frame`, `tele_arm`, …) plus the two multi-statement helpers `tele_put16` / `tele_field` as real `static` functions |

Pick the file matching your program's language — they expose the same protocol,
so a game can move between asm and C without the harness noticing.

## Register window (`$C440-$C443`)

| Addr | R/W | Name | Meaning |
|---|---|---|---|
| `$C440` | W | `TELE_DATA`  | push one byte into the outbound frame |
| `$C441` | W | `TELE_CTRL`  | control opcode (`TELE_END` / `TELE_LOCKSTEP` / `TELE_FREERUN` / `TELE_SCHEMA`) |
| `$C441` | R | `TELE_STAT`  | status bits (`TELE_CONNECTED` b7, `TELE_INAVAIL` b0) |
| `$C442` | R | `TELE_IN`    | pop one inbound byte (0 if none) |
| `$C443` | R | `TELE_INLEN` | inbound bytes pending (saturates at 255) |

## Wire format (self-describing schema frames, v1)

Emit a **schema frame** ONCE at startup describing your fields by name+type, then
one **data frame** per tick carrying the values. The harness keeps the last
schema and decodes each data frame field-by-field (raw-byte fallback if it never
saw a schema). Both frames flush over the same window:

- DATA frame   — `0xAA len_lo len_hi payload`, closed by `TELE_END` (`$01`).
- SCHEMA frame — `0xA5 len_lo len_hi payload`, closed by `TELE_SCHEMA` (`$03`);
  payload = `[type][name…][$00]` per field. Never parks lock-step.

Field type codes: `TELE_T_U8`=1, `TELE_T_S8`=2, `TELE_T_U16`=3, `TELE_T_S16`=4,
`TELE_T_BOOL`=5, `TELE_T_CHAR`=6.

### Lock-step vs free-run

`TELE_LOCKSTEP` (`tele_arm()`) makes every frame park the CPU on the `TELE_CTRL`
write until the harness sends one ACK (`$06`, consumed by POM1, never delivered
to `TELE_IN`) — reproducible regardless of host speed. `TELE_FREERUN`
(`tele_freerun()`) disarms it for a live fire-hose tap.

## Usage

C (cc65):

```c
#include "telemetry.h"
tele_field(TELE_T_U8,   "head_x");      /* declare the schema once … */
tele_field(TELE_T_U8,   "head_y");
tele_field(TELE_T_BOOL, "alive");
tele_schema_close();                    /* … emits the 0xA5 schema frame */
tele_freerun();                         /* or tele_arm() for lock-step    */
/* per tick: */
tele_put(head_x); tele_put(head_y); tele_put(alive);
tele_frame();                           /* emits the 0xAA data frame      */
```

asm (`.include`):

```asm
.include "telemetry.inc"
    TELE_FIELD TELE_T_U8, "head_x"      ; schema, once
    TELE_FIELD TELE_T_U8, "head_y"
    TELE_SCHEMA_FRAME
    TELE_ARM                            ; lock-step
    ; … per tick …
    TELE_PUT head_x
    TELE_PUT head_y
    TELE_FRAME
```

## Source of truth (asm ↔ C)

`telemetry.h` mirrors `telemetry.inc` **byte-for-byte**: the register addresses,
control opcodes, status bits and schema type codes are declared on both tracks.
**`telemetry.inc` is canonical** — edit it first, then sync the header.
`tools/check_lib_equates.py` (run by `make -C dev/lib check`) fails if any of the
shared `TELE_*` constants drift apart, so the "byte-for-byte" promise is now
enforced rather than trusted. The `0xAA`/`0xA5` frame sentinels live in the POM1
C++ consumer and `tools/pom1_telemetry.py`, not in these libs.
