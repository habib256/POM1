# `roms/` — what each image is, and where it came from

*[← Doc map](../doc/README.md) · [Contributing](../CONTRIBUTING.md)*

Sixteen ROM images plus the CodeTank cartridges in [`codetank/`](codetank/).
POM1 loads them at boot or when a card is plugged; none is generated at build
time except where this file says so.

**Why this file exists.** Two of these images are reproducible from published
sources and already document themselves (`ehbasic`, `msbasic`); the rest carried
no provenance anywhere in the tree. That is the kind of gap that cannot be filled
later by anyone but the person who assembled the collection — so it is written
down here while it still can be.

**Confidence is marked.** Lines tagged **⟨to confirm⟩** are POM1's best
reconstruction from the code, the credits and public sources, not first-hand
knowledge of which dump was taken from where. Treat them as claims to check, not
as facts. Everything untagged is verifiable from this repository.

## Inventory

| File | Bytes | SHA-256 (first 12) | What it is |
|---|---:|---|---|
| `WozMonitor.rom` | 256 | `e5af0d1c4057` | The Woz Monitor at `$FF00` — Steve Wozniak, 1976 |
| `basic.rom` | 4096 | `5d8a3fe3c20c` | Apple-1 Integer BASIC at `$E000` — Wozniak, 1976 |
| `ACI.rom` | 256 | `1b9fcf6f3e9e` | Apple Cassette Interface firmware at `$C100` — Wozniak, 1976 |
| `XACI.rom` | 256 | `e7f31b07e485` | Uncle Bernie's **Extended ACI** PROM page at `$C500` |
| `charmap.rom` | 1024 | `4db029004801` | Apple-1 text glyph set (the Signetics 2513 character generator) |
| `apple2e_char.rom` | 4096 | `52c3b87900ac` | Apple IIe Enhanced US character generator — see below |
| `krusader-1.3.rom` | 8192 | `b6097331da38` | Krusader 1.3 editor/assembler/debugger at `$E000` |
| `cffa1.rom` | 8160 | `ada5c4c1a918` | CFFA1 CompactFlash firmware at `$9000` |
| `sdcard.rom` | 8177 | `6a5e5a9fa926` | P-LAB **SD CARD OS 1.3** at `$8000` |
| `jukebox.rom` | 262144 | `cd045b0ec389` | P-LAB Juke-Box cartridge image (256 KB, bank-switched) |
| `applesoft-lite-microsd.rom` | 8192 | `521a448ad08b` | Applesoft Lite, microSD card-RAM flavour (`$6000`) |
| `applesoft-lite-cffa1.rom` | 8192 | `b17257079154` | Applesoft Lite, CFFA1 flavour (`$E000`) |
| `applesoft-gen2.rom` | 10236 | `0547aa1d115a` | Applesoft + the Apple-II graphics command set, GEN2 build |
| `logo-gen2.rom` | 17884 | `8f5e69f0d221` | APPLE-1 LOGO V2.6 turtle interpreter, GEN2 build (`$6000`) |
| `ehbasic.rom` | 12288 | `6b8beca2a093` | Enhanced 6502 BASIC 2.22, POM1's own Apple-1 port (`$5000`) |
| `msbasic.rom` | 8192 | `bbe7bfe7b1c5` | Microsoft BASIC 6502, Apple-1 build (`$E000`) |

`WozMonitor.rom` is the one image POM1 also carries **compiled in**, as the
fallback for a machine that cannot find `roms/` at all. `src/Memory.cpp` records
its SHA-256 in a comment; what actually prevents the two copies drifting is
`tests/rom_fallback_smoke_test.cpp`, which asserts they are byte-identical.

## 1. Apple firmware, 1976–1977

`WozMonitor.rom`, `basic.rom`, `ACI.rom`, `charmap.rom`, `apple2e_char.rom`,
`applesoft-lite-*.rom`

These are **Apple Computer's own firmware**, written by Steve Wozniak (the
Monitor, Integer BASIC and the ACI, all 1976) or derived from Apple's Apple II
line (the character generators, Applesoft). Apple has never placed them under a
free licence, and POM1 does not claim one: they are included because an Apple-1
emulator without the Woz Monitor does not boot at all, and because the Apple-1
replica and emulation community has distributed these 256- and 4096-byte images
openly for decades. If Apple ever objects, the answer is to remove them — the
emulator has a built-in Monitor fallback and every other image is optional.

**`apple2e_char.rom` deserves its own note**, and the reason is already in
`src/GraphicsCard.cpp`: Uncle Bernie's GEN2 release card carries a 2716
character-generator EPROM on the Apple-1's Signetics 2513 footprint,
reprogrammed with the Apple IIe full-ASCII glyph set. **That exact 2716 dump is
not published**, so POM1 ships the Apple IIe Enhanced US 4 KB character ROM
instead, which carries the same glyphs. It is the same file POM2 uses.

⟨to confirm⟩ Where each of these six images was originally obtained — which
archive, which dump, which date. The bytes are stable and hashed above; only the
sourcing is unrecorded.

## 2. Community firmware, named authors

| Image | Author | Origin | Licence |
|---|---|---|---|
| `krusader-1.3.rom` | **Ken Wessen** | Krusader 1.3 (65C02), released 2007-12-24 for the Replica 1. Source: <https://github.com/st3fan/krusader> | ⟨to confirm⟩ **not stated upstream** — the published source carries no licence file |
| `cffa1.rom` | **Rich Dreher** | CFFA1 CompactFlash interface firmware. Added to POM1 2026-04-11. | ⟨to confirm⟩ |
| `sdcard.rom` | **Antonino "Nippur72" Porcino** | P-LAB SD CARD OS 1.3. The *source* is vendored and fully documented in [`../doc/sdcardos_nippur72_1.3/POM1_PROVENANCE.md`](../doc/sdcardos_nippur72_1.3/POM1_PROVENANCE.md) (repo, commit `8adb29c`, fetched 2026-07-05) | **CC BY 4.0**, per the P-LAB project pages |
| `jukebox.rom` | **P-LAB** (Claudio Parmigiani & Jacopo Rosselli) | Assembled from P-LAB's EPROM_CREATOR stripped files by [`../doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py`](../doc/JUKEBOX_ROM_CREATOR/build_jukebox_rom.py), which is in-tree | ⟨to confirm⟩ presumably the P-LAB terms (CC BY 4.0) that cover the other P-LAB cards |
| `XACI.rom` | **Uncle Bernie** | The Extended ACI's second PROM page, published on Applefritter (août 2026). Behaviour documented in `CLAUDE.md` and pinned by `extended_aci_smoke`. | ⟨to confirm⟩ |

Krusader, the CFFA1 and the GEN2 card are all credited by name in
[`../README.md`](../README.md); this table records what those credits mean for
each *file*.

## 3. Reproducible from published sources

These are **built, not found** — the tree carries the source and the build
script, so a successor can regenerate them and verify the bytes.

| Image | Build | Licence position |
|---|---|---|
| `ehbasic.rom` | [`../dev/ehbasic/`](../dev/ehbasic/) — `build_ehbasic.sh`. The Apple-1 port is POM1's own; upstream shipped no such port. | Documented in [`../dev/ehbasic/README.md`](../dev/ehbasic/README.md). Lee Davison's terms are quoted there in full, and the string `DERIVED FROM EHBASIC` in the image is a **licence condition**, not decoration — `ehbasic_smoke` asserts it is still present. |
| `msbasic.rom` | [`../dev/msbasic/`](../dev/msbasic/) — `build_msbasic.sh`, from pinned commits of `mist64/msbasic` and a `coopzone-dc` overlay; the script asserts the resulting hash. | Documented in [`../dev/msbasic/README.md`](../dev/msbasic/README.md), including the fact that **neither upstream repository carries a licence file**. |
| `codetank/*.rom` | [`../tools/build_codetank_rom.py`](../tools/build_codetank_rom.py), from sources under `../dev/codetank/` and `../sketchs/`. `tools/verify_codetank_roms.py` is the burn gate. | POM1's own, plus the per-program credits in `../dev/codetank/README.md`. |

`applesoft-gen2.rom` and `logo-gen2.rom` are **DevBench sketches**, which is why
they are not in `dev/`: each is an ordinary asm project under `sketchs/gen2/`
with a `.sketch.json` telling the Bench how to build it.

| Image | Sketch | Links at | Entry |
|---|---|---|---|
| `applesoft-gen2.rom` | [`../sketchs/gen2/applesoft_gen2/`](../sketchs/gen2/applesoft_gen2/) — `applesoft-gen2.s` + `io.s`, cfg `applesoft_gen2.cfg` (`gen2gfx.inc`, `macros.s`, `zeropage.s` are `.include`d) | `BASROM $9800`, size `$2800` | `9800R` |
| `logo-gen2.rom` | [`../sketchs/gen2/tool_logo_gen2/`](../sketchs/gen2/tool_logo_gen2/) — `logo_gen2.asm`, cfg `logo_gen2.cfg`, defines `CODETANK_BUILD` + `LOGO_GEN2`, plus seven library modules from `dev/lib/` named in its `.sketch.json` | `CODE $6000`, size `$5000` | `6000R` |

Both build on POM1 preset 2 (GEN2 HGR Development Bench): open the sketch in the
Bench and Build. Their behaviour is pinned by `applesoft_gen2_smoke` and by
`bench_logo_inject_smoke` respectively, so a rebuild that shifts an entry point
fails the suite rather than shipping quietly.

**Installing a rebuilt image into `roms/` is manual** — no script copies it, and
the shipped `.rom` files are prebuilt artefacts of exactly this path.

## What still needs the maintainer

Everything tagged **⟨to confirm⟩** above, which is:

1. The sourcing of the six Apple images (§1) — which archive or dump, and when.
2. The licence position of Krusader, the CFFA1 firmware, the Juke-Box image and
   the Extended ACI page (§2).
Nothing here blocks running or building POM1. It matters the day someone else
has to answer for the collection.
