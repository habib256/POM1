# Microsoft BASIC 6502 for the Apple-1 (`roms/msbasic.rom`)

An 8 KB Microsoft BASIC with **floating-point maths**, sharing the `$E000`
window with Woz's Integer BASIC. That is the whole point of having it: Integer
BASIC has no floats at all.

```
E000R   cold start   (asks MEMORY SIZE? and TERMINAL WIDTH? — RETURN takes the defaults)
E003R   warm start   (keeps the program in RAM)
```

In POM1: **Settings → Memory Settings → ROM Loading → "Load Microsoft BASIC"**.
Loading it evicts Integer BASIC and vice versa — one BASIC EPROM socket, one
interpreter, as on real hardware. Pinned by the `msbasic_smoke` test, which
boots the ROM on the emulated 6502 and asks it to compute `1/4`.

## Provenance

| Piece | Source | Pinned at |
| --- | --- | --- |
| The interpreter | [mist64/msbasic](https://github.com/mist64/msbasic) — the integrated source tree that builds nine historical Microsoft BASIC variants | `2a0bc2fe0db13f8cf1b5c40b1d5617263cdb9cb4` |
| The Apple-1 port (`overlay/`) | [coopzone-dc/Apple-1-Replica](https://github.com/coopzone-dc/Apple-1-Replica), `msbasic/` | `4e6fc07a7bb20b52fcc53437fc54eb0055f68821` |
| Target dispatch (`patches/`) | written here — see below | — |

The build is **OSI-derived** (`CONFIG_10A`, `CONFIG_SMALL` so it fits 8 KB) with
the I/O rewritten for the Apple-1 PIA: `overlay/bios.s` reads `KBDCR`/`KBDD` and
writes `DSP`, `overlay/cz6502_iscntc.s` implements Ctrl-C break the same way, and
`overlay/reset.s` sets NMI `$0F00` / IRQ `$0000` — the authentic Woz Monitor
vectors POM1 already preserves (see CLAUDE.md, `configureResetVectors`).

### Why a patch is needed

The overlay as published does not assemble: mist64/msbasic selects a target
through four dispatch points, and the overlay author never published the
branches for theirs (the Applefritter thread where this ROM circulates ends with
"he provided the missing parts" — those parts were never posted). The patch adds
them:

| File | Branch added |
| --- | --- |
| `defines.s` | `.elseif .def(cz6502)` → `CZ6502 := 1` + `defines_cz6502.s` |
| `extra.s` | `.ifdef CZ6502` → `cz6502_extra.s` |
| `iscntc.s` | `.ifdef CZ6502` → `cz6502_iscntc.s` |
| `header.s` | `.ifdef CZ6502` → the two entry `JMP`s at `$E000`/`$E003` |

The `header.s` one is load-bearing and was the last piece: without it the image
starts with the VECTORS segment instead of the jump pair, so `E000R` executes a
`BIT` into the middle of a table rather than cold-starting BASIC.

## Rebuilding

```sh
sh dev/msbasic/build_msbasic.sh            # build + verify, leave it in a temp dir
sh dev/msbasic/build_msbasic.sh --install  # ...and copy over roms/msbasic.rom
```

The script asserts `sha256 = bbe7bfe7b1c518c0e54e741e5df1c0170572751bbf41374a9d5898f03ac642aa`.
That hash is **also the hash of `roms/cz6502.bin` published by coopzone-dc** — the
build reproduces the circulating ROM byte for byte, which is what makes the
committed artefact auditable rather than a blob of unknown origin.

## Layout, and what POM1 actually maps

The file is the replica's full 8 KB EPROM image:

```
$E000-$FEFF   Microsoft BASIC
$FF00-$FFF9   a Woz Monitor copy   (the replica's EPROM had to supply one)
$FFFA-$FFFF   NMI $0F00 / RESET / IRQ $0000
```

`Memory::loadMsBasic` loads it whole and then reloads POM1's own
`WozMonitor.rom` on top, so the canonical monitor image stays in charge. The
file is kept intact rather than trimmed to `$1F00` so its hash still matches the
published ROM.

## Licence

Neither upstream repository carries a licence file. Microsoft BASIC's 6502
sources have circulated publicly for decades and mist64's tree is the reference
reconstruction, but no explicit redistribution grant exists — the same footing as
the Integer BASIC and Applesoft images POM1 already ships. Flagged here so the
position is a decision on record rather than an oversight.
