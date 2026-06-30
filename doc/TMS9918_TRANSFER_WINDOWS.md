# TMS9918 transmission windows — CPU → VRAM transfer zones

How fast the Apple-1 6502 may push bytes into the P-LAB Graphic Card's VRAM
(`$CC00` data / `$CC01` control) is **not constant** — it depends on what the
VDP's raster is doing at that instant. The chip and the CPU share the VRAM bus;
the CPU may only write when the VDP isn't fetching for display. POM1 models this
in `siliconStrictMode` and drops a write that arrives before the bus is free
again. This doc is the precise reference for the **transmission zones**, **how
POM1 detects them**, and **the minimum write spacing each one allows**.

Single source of truth in code: `TMS9918::transmissionZone()`
(`src/TMS9918.cpp`). `selectSlotTable()`, `activeSlotTableId()`,
`noteAcceptedAccess()` (the drop-detection drain) and `noteDroppedAccess()` (the
diagnostics) **all derive from it**, so the gating and the accounting can never
disagree about which zone a write landed in.

## Detection inputs

`transmissionZone()` decides from exactly three facts about the live chip
state — no history, no heuristics:

| Input | Source | Meaning |
|-------|--------|---------|
| display-enable | `regs[1] & 0x40` (R1 bit 6) | 1 = visible raster on, 0 = blanked |
| vertical retrace | `frameCycleCounter >= kActiveDisplayCycles` | in VBlank (last ~70 of 262 NTSC lines) |
| active video mode | R1 bit 4 (M1=Text), R1 bit 3 (M2=Multicolor), else Graphics I/II | only consulted while visible |

`kActiveDisplayCycles = (kCyclesPerFrame * 192) / 262` — the **same** threshold
that raises the status F-flag in `advanceCycles()`, so the zone boundary and the
status-bit edge are bit-for-bit consistent.

Detection order (first match wins):

```
display OFF (R1.6 = 0) ............................ Blanked
else in vertical retrace .......................... VBlank
else M1 (R1 bit 4) set ............................ ActiveText
else M2 (R1 bit 3) set ............................ ActiveMulti
else .............................................. ActiveGfx12
```

## The five zones

| Zone | Detection | Slot table | Min write gap to avoid a drop |
|------|-----------|-----------|-------------------------------|
| **Blanked** | display OFF, any line | `slotsMsx1ScreenOff` | **~2 c** (free) |
| **VBlank** | display ON, vertical retrace | `slotsMsx1ScreenOff` | **~2 c** (free) |
| **ActiveGfx12** | display ON, visible, Graphics I/II | `slotsMsx1Gfx12` | slot-derived (≤ ~7 c) |
| **ActiveText** | display ON, visible, Text (M1) | `slotsMsx1Text` | slot-derived (~2 c) |
| **ActiveMulti** | display ON, visible, Multicolor (M2) | `slotsMsx1Gfx3` | slot-derived (≤ ~5 c) |

Timing is **pure openMSX**: D28 prep delay + the mode's slot table, with **no
artificial floor** (the retired POM1 `kMinActiveDrainCycles = 9`). The exact
minimum gap is position-dependent (it depends where in the 1368-tick line the
write lands); the values above are representative worst cases.

### Free zones — `Blanked` + `VBlank`

The raster isn't fetching VRAM (display off, or in retrace), so the bus is
**ungated**. The dense `ScreenOff` slot table grants a slot every 8 VDP ticks
(~0.4 c); the only residual cost is the chip's D28 prep (28 ticks → `ceil(28/21)
= 2 c`). Because every real 6502 store is already ≥ 4 c apart,
`noteAcceptedAccess()` books **zero drain** in a free zone — **a write here can
never drop.**

This is the period for bulk transfers: cold-boot VRAM init and big uploads. The
canonical idiom is *blank → burst → unblank*:

```
LDA #$80 / STA $CC01 / LDA #$81 / STA $CC01   ; R1.6 = 0  (enter Blanked zone)
... set VRAM write address ...
@b: STA $CC00 / INY / BNE @b                   ; tight burst, NO per-byte pad
LDA #$C0 / STA $CC01 / LDA #$81 / STA $CC01   ; R1.6 = 1  (display back on)
```

The lib uses exactly this in `init_vdp_g1` (`wipe_all_vram`) and `init_vdp_g2`
(`dev/lib/tms9918/tms9918m1.asm` / `tms9918m2.asm`): the 16 KB / 6 KB clears run
in the Blanked zone with **no `tms9918_pad18` between bytes** — ~3× faster than a
padded loop. Per-frame updates that can't blank should instead poll `$CC01` and
burst during the VBlank zone (same free bandwidth, no visible blank).

### Gated zones — `ActiveText` / `ActiveMulti` / `ActiveGfx12`

While the visible raster fetches name/pattern/colour/sprite data, the CPU only
gets the slots the mode leaves free (`slotsMsx1Gfx12` / `slotsMsx1Text` /
`slotsMsx1Gfx3`, all ported verbatim from openMSX `VDPAccessSlots.cc`).

- **ActiveGfx12** (Graphics I/II) is the tightest: sparse slots in mid-line, so
  a sub-~7 c burst out-paces them and drops. The lib's `tms9918_pad18` (22 c
  STA→STA) clears any Gfx12 slot gap with margin.
- **ActiveText** / **ActiveMulti** publish denser slot tables, so tight text /
  multicolor loops that would drop in Gfx12 stay valid here.

POM1 follows openMSX's MSX1 timing exactly — there is **no `kMinActiveDrainCycles`
floor** (it was a deliberate POM1 divergence, retired June 2026 in favour of
pure slot-table + D28 timing).

## The drop model — deferred, newest-wins (openMSX `scheduleCpuVramAccess`)

POM1 ports openMSX's exact CPU↔VRAM mechanism (`src/video/VDP.cc`):

1. A data-port access (or a `$CC01` read-address-setup prefetch) is **not
   executed immediately** — it is *scheduled* to the next VRAM slot
   (`beginPendingAccess`) and run later by `executeCpuVramAccess()` when the
   slot's cycle is reached in `advanceCycles()`. The VRAM pointer advances only
   at execute time.
2. If a new access arrives while one is still pending, it is **too fast**: the
   newer request **overwrites** the pending one (`readAheadBuffer` is the shared
   `cpuVramData`) and the single scheduled slot fires once with the **newer**
   byte. So of two collided writes to one cell, the **latest value wins** and the
   pointer advances **once** — exactly openMSX with `allowTooFastAccess=off`
   (whose `tooFastCallback` is only a notification hook).
3. POM1 additionally tallies every too-fast event as a **drop** for diagnostics
   (openMSX has no built-in counter).

Control-port ($CC01) register / write-address writes are **gated** (a deliberate
POM1 divergence kept on top of openMSX, which leaves them ungated): a control
write arriving while a VRAM access is pending is dropped. They schedule a
one-slot `Barrier` so back-to-back control bytes are gated too.

## Drops correspond to zones

A dropped write is, by construction, a write that violated **its zone's**
minimum gap. The diagnostics (`TMS9918::dropDiagnostics()`,
`dumpDropDiagnostics()`) reflect that:

- `byTable[]` counts drops per slot table = per zone family.
- `inActive` = drops in a **gated active-display zone** — the only place drops
  are expected.
- `inVBlank` = drops in a **free zone** (Blanked | VBlank) — **anomalous**
  (the program out-paced even the ~2 c window; in practice impossible for real
  6502 code). Classified by the *zone*, not by raw frame position, so a write
  during a blanked active-region line is correctly counted as free, not active.
- The per-drop stderr trace prints `zone=<Blanked|VBlank|ActiveGfx12|
  ActiveText|ActiveMulti>` alongside the slot table and line position.

## Tests

`tests/tms9918_silicon_strict_runtime_test.cpp` pins the per-zone behaviour:
Phase B/B2 (Gfx12 4 c drops / 8 c lands — pure slot table, no floor), Phase C
(Gfx12 4 c burst drops), Phase D (Text 5 c accepts), Phase E (Multicolor 6 c
accepts), Phase F (Blanked 2 c flood accepts), Phase G (VBlank-with-display-ON
2 c flood accepts), **Phase K (newest-wins: the later of two collided bytes is
what lands, pointer advances once)**. Phase J pins the `inActive + inVBlank ==
total` partition. `tms9918_sprite_status` T8 also pins newest-wins end-to-end.
