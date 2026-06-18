# GEN2TextHello — hello-world TEXT 40×24 demo on the GEN2 card

*[← POM1 documentation index](../../../../doc/README.md)*

Small **C** program for Uncle Bernie's GEN2 colour card that prints two lines
in **TEXT mode 40×24**:

```
            hello world
       Uncle Bernie's GEN2 Color Card
```

It is first and foremost a showcase for the **Apple IIe Enhanced US char ROM**
POM1 now loads to render GEN2 TEXT (`roms/apple2e_char.rom`, consumed from
`src/GraphicsCard.cpp`). POM1's legacy char-gen — a hardcoded 5×7 ASCII font
kept as a fallback — folded every lowercase letter back to uppercase; the
Apple IIe ROM renders true lowercase glyphs with descenders, which is exactly
what this demo highlights.

P-LAB context: on the real card, Bernie's 2716 char-gen is physically derived
from the Apple-1's Signetics 2513 footprint but reprogrammed with the Apple II
glyphs (no dump published yet); POM1 models that by consuming the Apple IIe
dump and only indexing the first 2 KB (the primary set) — the GEN2 has no
alt-set selector.

## Build

```sh
make            # -> "software/Graphic HGR/GEN2TextHello.bin" (+ .txt Woz-hex)
make clean
```

## Run

```sh
build/POM1 --preset 11 \
    --load 6000:"software/Graphic HGR/GEN2TextHello.bin" --run 6000
```

Or via the **POM1 Bench**: *DevBench → POM1 Bench → New sketch → C ×
"Uncle Bernie GEN2 HGR"*, paste the contents of `GEN2TextHello.c` and hit
**Run**.

## The code in brief

```c
gen2_text();        /* $C251 - TEXT_ON */
gen2_full();        /* $C252 - MIXED off */
gen2_page1();       /* $C254 - page 1 */
text_clear();       /* fill $0400-$07FF with $A0 (NORMAL spaces) */
putstr(14, 11, "hello world");
putstr(5,  13, "Uncle Bernie's GEN2 Color Card");
for (;;) { gen2_text(); }   /* re-assertion = covers the DevBench deferred plug */
```

Screen-byte encoding (Apple II convention): bit 7 forced → **NORMAL** attribute
(white-on-black), low 7 bits = raw ASCII. That is the `$80-$FF` branch of the
char-gen — see `dev/lib/gen2c/gen2.h` and `src/GraphicsCard.cpp::resolveGlyph`
for the inverse / flashing / normal machinery.

TEXT page 1 lives at `$0400-$07FF`, laid out in **Apple II interleave**:

```
addr(row) = $0400 + 0x80 × (row & 7) + 0x28 × (row >> 3)
```

(same formula as `GraphicsCard::textRowAddress` on the emulator side.)
