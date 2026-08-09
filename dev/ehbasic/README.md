# Enhanced 6502 BASIC on the Apple-1 (`roms/ehbasic.rom`)

**Derived from EhBASIC.**

Lee Davison's Enhanced 6502 BASIC 2.22 — floating point, string handling,
`IF..THEN..ELSE`, `DO..UNTIL`, hex and binary literals, `MAX`/`MIN`, bitwise
operators — running on an Apple-1. Far beyond any BASIC the machine actually
shipped with.

```
5000R   cold start   (asks "Memory size ?" — RETURN probes it for you)
5003R   warm start   (keeps the program in RAM)
```

In POM1: **Settings → Memory Settings → ROM Loading → "Load EhBASIC 2.22"**.

## This port is POM1's own

No Apple-1 port of EhBASIC existed. Upstream ships `min_mon.asm`, a host stub
for Michal Kowalski's 6502 simulator: a simulated ACIA at `$F000`, entry through
the 6502 RESET vector, and a `[C]old/[W]arm ?` prompt. None of that fits an
Apple-1, whose RESET vector belongs to the Woz Monitor.

`src/apple1_mon.asm` replaces it:

| Upstream | Here |
| --- | --- |
| simulated ACIA at `$F000` | the real PIA 6821 — `KBDCR`/`KBD` in, `DSP` out, Woz's own ECHO wait-loop |
| entry via the RESET vector | two `JMP`s pinned at `$5000`/`$5003`, entered from the monitor like every other POM1 interpreter (`6000R`, `E000R`) |
| `[C]old/[W]arm ?` prompt | none — the address you type already says which you want |

`src/apple1.s` holds the machine configuration (PIA addresses, `RAM_BASE`,
`RAM_TOP`), `src/apple1.cfg` the layout. **`basic.asm` — the interpreter — is
untouched.**

## Memory layout

```
$0000-$0002   EhBASIC's warm-start JMP  (its LAB_WARM is at $00)
$0100-$01FF   stack
$0200-$0268   I/O vectors, CTRL-C flags, input buffer
$0300-$4FFF   user RAM — BASIC programs and variables (~19 KB free)
$5000-$78B6   the interpreter (~10.4 KB); $78B7-$7FFF unused
```

It is **loaded into RAM, not into a ROM window** — the same model as Applesoft
Lite on the P-LAB microSD card. Two consequences:

- **It needs at least 32 KB.** Everything sits below `$8000`, so a 32 KB machine
  runs it; the 8 KB presets cannot (the interpreter alone is bigger).
- **Parmigiani's one-board rule applies.** Any card decoding inside
  `$5000-$7FFF` shadows it: the microSD Applesoft window (`$6000-$7FFF`),
  CodeTank (`$4000-$7FFF`), the Juke-Box (`$4000-$BFFF`). POM1 unplugs exactly
  those three when you flash it, and says so in the status bar.

`$5800-$7FFF` — the window other Apple-1 EhBASIC discussions assume — is 183
bytes too small for a full 2.22 build. `$5000` is the next round address that
holds it with room to spare.

### Why the interpreter is not at `$9000`

Putting it high would leave ~35 KB for programs instead of ~19 KB, but it would
require 48 KB and collide with more cards (microSD's SD CARD OS, CFFA1, the
modem ACIA). ~19 KB is already several times what any period Apple-1 BASIC
offered, so the low placement wins on compatibility.

## Provenance

| Piece | Source | Pinned at |
| --- | --- | --- |
| The interpreter (`basic.asm`) | [jfredrickson/ehbasic-cc65](https://github.com/jfredrickson/ehbasic-cc65) — Lee Davison's 2.22 with only the syntax changes ca65 needs | `204318b585ac09faa8ded83fceeeb2e3bdf524f4` |
| The Apple-1 port (`src/`) | written here | — |

The original 2.22 source is [Klaus2m5/6502_EhBASIC_V2.22](https://github.com/Klaus2m5/6502_EhBASIC_V2.22).
It is written for Kowalski's assembler and uses `[...]` for expression grouping,
which `ca65` rejects; the cc65 port above is that conversion, and starting from
it keeps this directory to the Apple-1 work alone.

## Rebuilding

```sh
sh dev/ehbasic/build_ehbasic.sh            # build + hash check, temp dir
sh dev/ehbasic/build_ehbasic.sh --install  # ...and copy over roms/ehbasic.rom
ctest -R ehbasic_smoke                     # then always re-run the test
```

`ehbasic_smoke` boots the image on the emulated 6502, checks `PRINT 1/4` gives
`.25` and `PRINT SQR(2)` gives `1.41421`, then types in a `FOR` loop and runs
it. Since this port has no published image to diff against, **running the
interpreter is the verification** — do not ship a rebuilt ROM that has not
passed it.

## Licence

EhBASIC is free but **not** copyright free. Lee Davison's terms, quoted from
upstream's `readme.txt`:

> EhBASIC is free but not copyright free. For non commercial use there is only
> one restriction, any derivative work should include, in any binary image
> distributed, the string "Derived from EhBASIC" and in any distribution that
> includes human readable files a file that includes the above string in a human
> readable form e.g. not as a comment in an HTML file.
>
> For commercial use please contact Lee Davison at leeedavison@googlemail.com
> for conditions.

Both halves are satisfied: **this file** is the human-readable one (the string
appears in its first line), and the port prints `DERIVED FROM EHBASIC` at
sign-on, so it is present in the distributed binary. `ehbasic_smoke` asserts
that string is still in the image — treat that assertion as a licence condition,
not a stylistic one.
