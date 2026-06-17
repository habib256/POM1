# TMS Clone — sprite cloning bug exhibition (Bug N°8)

*[← POM1 documentation index](../../../../doc/README.md)*

Visual regression fixture for POM1's port of meisei `vdp.c:591-670`
(2026-05) — the silicon-faithful sprite cloning algorithm by hap.
6502 port of the BASIC test program from
[openMSX issue #593](https://github.com/openMSX/openMSX/issues/593).
See [`dev/SILICONBUGS.md`](../../SILICONBUGS.md) §9 for the full
silicon spec.

## Background

The TMS9918 has 8 valid combinations of the M1/M2/M3 mode bits. When
≥ 2 bits are simultaneously active ("hybrid illegal mode"), the chip
silently enters chaotic-but-reproducible behaviour: the SAT sprites
produce **ghost clones** in the top strate (Y=0..63), formed by an
internal address-line leak between the SPGT (R6) and color-table (R3)
fetch paths.

The MSX demo "Alankomaat" by Bandwagon (2001) deliberately uses this
to display more sprites than the chip officially supports.

POM1 models this in `TMS9918.cpp` via:

```
Y_polluted = ((slot * 8) ^ ((R3 & $60) | (R6 & $07))) & $3F
```

This formula is **a simplified approximation** — the real silicon
motif depends on the microarchitecture of the SAT fetch and is not
fully documented. POM1's version produces a visible cascade that
matches the broad behaviour ("ghosts in the top strate") but the exact
Y values may differ from what real TI/NMOS silicon shows. Toshiba
clones and Yamaha V9938 have factory-fixed addressing and produce
**no clones at all**.

## What this demo does

1. Boot in **Mode I** (M1=0, M2=0, M3=0 — legal). Striped background:
   - rows 0..7 = blank ("sky")
   - rows 8..15 = solid white ("ground")
   - rows 16..23 = blank ("underground")
2. 8 white "torch" sprites at Y=80 (≈ row 10), animated horizontally
   with a 24-px stagger.
3. Press **SPACE** → toggle into **illegal hybrid M1+M2** by setting
   R0 bit 1 AND R1 bit 4. The background turns to backdrop-only
   (black), the originals stay at Y=80, and 8 clones appear cascading
   in the top strate at:

   ```
   slot 0 → Y=7      slot 4 → Y=39
   slot 1 → Y=15     slot 5 → Y=47
   slot 2 → Y=23     slot 6 → Y=55
   slot 3 → Y=31     slot 7 → Y=63
   ```

   (Computed from POM1's formula with our R3=$80, R6=$07.)
4. Press **SPACE** again → back to Mode I, clones disappear, striped
   background returns. The originals at Y=80 keep animating throughout.
5. **ESC** → exit to Wozmon.

## Validation matrix

| Target | Expected behaviour |
|---|---|
| **POM1 silicon-strict** | Clones MUST appear in illegal mode, MUST disappear in Mode I. Y values match the formula above. |
| **Real silicon TI / NMOS** | Clones appear, may fade as chip warms up (thermal drift, not modelled in POM1). |
| **Toshiba / Yamaha V9938** | Clones do NOT appear (factory-fixed addressing). |
| **openMSX / icy99** | Clones appear; exact Y values may differ (each emulator picks its own simplified formula). |

If POM1 silicon-strict fails to render clones in illegal mode, that's
a regression in the `renderCloneSpritesLineRaw` path — file a POM1
bug.

If POM1 renders clones IN LEGAL Mode I (where it shouldn't),
`isIllegalModeRegs` is over-triggering — also a POM1 bug.

## Hardware

- Apple 1 (stock 4 KB DRAM)
- P-LAB TMS9918 Graphic Card
- Recommended POM1 preset: 7 (P-LAB TMS9918 + CodeTank), or the P-LAB Multiplexing Fantasy preset 11

## Sources

- `TMS_Clone.asm` — main entry, builds background + SAT + sprite pattern
- `apple1_clone.cfg` — 4 KB stock layout with BSS at $0F80 (sat_buf)
- `emit_TMS_Clone_txt.py` — assemble + emit Woz hex
- libs used: `dev/lib/apple1/`, `dev/lib/tms9918/` (m1 + pad)

## Build

    make                       # → ../../../../software/Graphic TMS9918/TMS_Clone.{bin,txt}

## Run in POM1

1. POM1 → Hardware → plug TMS9918.
2. File → Load → `software/Graphic TMS9918/TMS_Clone.txt`.
3. Wozmon `\` prompt: `280R`.
4. Watch 8 torches animate at Y=80 against the 3-strate background.
5. Press **SPACE** — clones cascade in the top strate.
6. Press **SPACE** again — clones disappear.
7. **ESC** — back to Wozmon.

## Cross-references

- `dev/SILICONBUGS.md` §9 — Bug N°8 spec, POM1 implementation notes,
  thermal drift caveat
- `dev/projects/tms9918_silbench/` T12 (SPRITE CLONING) — same test
  inside the silicon-strict validation suite (this demo is the
  standalone visual variant, with toggle for direct comparison)
- `tms9918_silicon_strict_runtime` — POM1 lock-step regression (ctest)
- openMSX issue [#593](https://github.com/openMSX/openMSX/issues/593) —
  upstream discussion of the cloning model

## Author / License

VERHILLE Arnaud, 2026. License: GPL-3.0 (see repository LICENSE).
