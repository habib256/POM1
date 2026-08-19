# TMS Nyan CodeTank — RLE-compressed 12-frame Nyan in 16 KB

*[← POM1 documentation index](../../doc/README.md)*

CodeTank-resident variant of the Fantasy-preset Nyan demo:
same 12-frame animation (the FULL one from jblang's `nyan.bin`), but
RLE-compressed so the whole binary fits in a 16 KB CodeTank ROM slot
at $4000-$7FFF. Run-in-place from ROM — no RAM needed for the data.

## Compression

| | Raw | RLE | Ratio |
|---|---|---|---|
| 12 frames × 1 536 B | 18 432 B | 6 830 B | 37 % |
| Total binary (with code) | — | 7 201 B | — |
| 16 KB slot fill | — | 44 % | 9 183 B free |

The RLE format is a PackBits-flavoured stream:

| Header byte H | Meaning |
|---|---|
| 0..127  | output the next (H + 1) bytes literally |
| 128..255 | output (H − 127) copies of the next byte |

The decoder is ~50 bytes of 6502, streams output directly to `VDP_DATA`
without any RAM buffer. A 16-bit countdown stops decoding cleanly at
exactly 1 536 output bytes per frame.

## Run in POM1

1. Load **preset 9** (P-LAB Apple-1 with TMS9918 + CodeTank
   daughterboard) — TMS9918 is plugged by default, CodeTank too.
2. Hardware → CodeTank → ROM Library → select **Codetank_DEMOS.rom**
   (Nyan is one of its five lower-bank demos).
3. Hardware → CodeTank → Jumper → **Lower**.
4. Wozmon `\` prompt → type `4000R` — the demo picker comes up.
5. Pick **Nyan**; watch the cat bob and the rainbow scroll at ~20 fps.
6. ESC returns to Wozmon.

> Or just open this sketch in DevBench and **Run**.

## Build

Mono-source DevBench sketch (no Makefile). It ships in the `$6000` slot of
`Codetank_DEMOS.rom`'s **lower** bank, assembled from `TMS_Nyan_CodeTank.asm`
+ `nyan_rle.asm` (with `tms9918_pad` auto-linked) by:

    python3 tools/build_codetank_rom.py --rom=demos

## Why it fits when the Fantasy variant ships 19 KB raw

Same data — the RLE compression pays for itself entirely:

- Each Mode III frame has long runs of dark blue (`$44`) in the
  star-field area + medium-length runs of cat-body magenta (`$dd`)
  and rainbow stripes. PackBits-style encoding collapses each run to
  2 bytes (header + value) regardless of length up to 128.
- For frame 0: 1 536 B → 536 B compressed. The large `$44`
  "background" sections at the top + bottom collapse to single
  2-byte runs (well, 4 bytes since each section is 256 B and runs
  cap at 128).
- For mid-cycle frames the cat sprite consumes more space (more
  literal bytes for varied pixel data) but they still average
  ~570 B each.

## Memory map

    Host Apple-1:
      $0000-$007F  Zero page (~80 B)
      $0100-$01FF  6502 stack
    CodeTank ROM (DEMOS lower half of the 28c256 EEPROM):
      $6000-$7FFF  CODE — code (~700 B) + nyan_rle.asm (~6.8 KB)
                          + lib/tms9918_pad (~50 B)

The DEMOS lower bank is a menu of five run-in-place demos ($4000 menu /
$4200 Life / $4A00 Mandel / $5200 Plasma / $5A00 Vague / $6000 Nyan); the
upper bank holds `demo_sprite_animals`.

## Sources

- `TMS_Nyan_CodeTank.asm` — Mode III init + RLE decoder + animation loop
- `nyan_rle.asm` — auto-generated 12-frame compressed stream
- `apple1_nyan_codetank.cfg` — ld65 config (CODE at $4000, 16 KB)
- `.sketch.json` — DevBench metadata (cfg + extraAsm `nyan_rle.asm` + `tms9918_pad.asm`)
- Output: shipped in `Codetank_DEMOS.rom`'s lower bank, `$6000` slot (via `build_codetank_rom.py --rom=demos`)

## Author / License

VERHILLE Arnaud, 2026. Same lineage as the 4 KB and Fantasy variants —
algorithm and frame format from J.B. Langston (Z80 port, MIT licence —
see [jblang/TMS9918A](https://github.com/jblang/TMS9918A)); artwork is
[*Passan Kiskat*](http://www.dromedaar.com/) by Dromedaar Vision. RLE
compression is a stock PackBits derivative.
