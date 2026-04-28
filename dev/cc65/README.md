# cc65/ — shared cc65 linker configs + Woz-hex emitter

Shared linker configs reusable across projects, plus a single `emit_woz.py`
module that replaces the per-project `emit_*_txt.py` boilerplate.

## Linker configs — pick the right one

| Config | ZP | CODE start | CODE size | Reserved RAM | Use case |
|---|---|---|---|---|---|
| **`apple1.cfg`**       | $0000-$0022 (35 B) | $0280 | 3 328 B | — | Stock Apple-1, small text-mode programs |
| **`apple1_4k.cfg`**    | $0000-$0022 (35 B) | $0280 | 4 096 B | — | Stock 4 KB DRAM, mid-size text programs |
| **`apple1_8k.cfg`**    | $0000-$0022 (35 B) | $0280 | 7 552 B | — | Stock 8 KB DRAM, no expansion (text) |
| **`apple1_gen2.cfg`**  | $0000-$001F (32 B) | $0280 | 7 552 B | $2000-$3FFF (HGR FB) | GEN2 HGR projects |
| **`pom1.cfg`**         | $0000-$0022 (35 B) | $0300 | 40 192 B | — | Big programs (POM1-only, Krusader-RAM) |

### Decision rule

1. **Text-mode**: pick the smallest that fits. Start with `apple1.cfg`
   (3.3 KB), upgrade to `apple1_4k.cfg` if you overflow, `apple1_8k.cfg`
   if you still overflow.
2. **HGR (GEN2)**: always `apple1_gen2.cfg`. The `$2000-$3FFF` reservation
   matches the framebuffer.
3. **TMS9918**: VRAM is separate (16 KB I/O-only on the card), so pick
   based on your CODE budget: `apple1.cfg` for small games,
   `apple1_8k.cfg` if you grow.
4. **GT-6144**: the framebuffer lives on 6× Intel 2102 SRAM (no Apple-1
   RAM), so `apple1.cfg` or `apple1_8k.cfg` works. `gt6144_hello/gt6144.cfg`
   is a project-local copy that loads at `$0300` to leave `$0280` clear
   for ACI tape interaction (rarely needed; copy as a starting point).
5. **CodeTank ROM banks**: project-local `.cfg` (sokoban_codetank,
   galaga_codetank, etc.) with `CODE start = $4000` or `$5E00` etc. —
   the ROM lives at the CodeTank window, not in RAM.
6. **Big programs**: `pom1.cfg` only if you really need 40 KB and don't
   care about real-Apple-1 portability.

### Project-local configs

Some projects ship their own `.cfg` because they need:
- a custom CODE start address (CodeTank bank offsets),
- additional MEMORY regions (BSS, ring buffers, save buffers),
- different ZP size (TMS_Logo: 32 B; LINEBUF segment in $0200-$027F).

Pattern: when you copy a shared config locally and tweak it, document
the deviation in a comment header inside the cfg.

If a project's deviation generalises (e.g. "all 8 KB DRAM text
programs"), promote it to this directory and migrate consumers.

## emit_woz.py — shared Woz-hex emitter

Replaces the ~10 near-identical `emit_<PROJECT>_txt.py` scripts under
`dev/projects/`. Each project keeps a 5-line `emit_<PROJECT>_txt.py`
shim that imports `emit_woz` and calls `emit()` with project-specific
parameters.

### Module API

```python
from emit_woz import emit
emit(
    asm_files=["MyProject.asm"],         # one or more sources
    lib_dirs=["apple1", "hgr"],          # resolves to dev/lib/<name>/ -I paths
    cfg="apple1_gen2.cfg",                # project-local OR dev/cc65/ relative
    out_dir_software="hgr",               # software/<dir>/ for the .bin/.txt
    start_addr=0x0280,                    # load address used in the .txt suffix
    header_lines=["// optional banner"],  # prepended to the .txt
    project_dir=PROJ,                     # absolute path of the calling script
)
```

The function:
1. Assembles each `.asm` to `<root>/build/<stem>.o` with `ca65` and the
   library `-I` paths.
2. Links with `ld65` against the resolved `.cfg`.
3. Writes the `.bin` to `<root>/software/<out_dir_software>/<stem>.bin`.
4. Emits a Woz-hex `.txt` next to it (8 bytes per line, `<START>R` at
   the end so File > Load Memory + `R` runs the program).

### CLI mode

For one-shot projects without a custom shim:

```bash
python3 ../../cc65/emit_woz.py \
    --asm MyProject.asm \
    --lib apple1 --lib hgr \
    --cfg apple1_gen2.cfg \
    --out-software hgr \
    --start 0x0280
```

### Multi-module link order

Pass multiple `asm_files` in the order you want the linker to see them.
The first file's entry point lands at `CODE start` (typically $0280).
Library `.asm` sources outside the project use repo-relative paths:

```python
emit(asm_files=[
    "TMS_Logo.asm",
    "lib/m6502/math.asm",
    "lib/tms9918/tms9918m2.asm",
], …)
```

`emit_woz` resolves `lib/m6502/math.asm` against `<root>/dev/`.

### Migration path

Existing projects can convert their `emit_<NAME>_txt.py` to a 23-line
shim:

```python
#!/usr/bin/env python3
"""Assemble Foo and write Foo.txt — wraps emit_woz.py."""
import pathlib, sys
PROJ = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(PROJ.parents[1] / "cc65"))
from emit_woz import emit  # noqa: E402

def main() -> int:
    emit(
        asm_files=["Foo.asm"],
        lib_dirs=["apple1", "hgr"],
        cfg="apple1_gen2.cfg",
        out_dir_software="hgr",
        start_addr=0x0280,
        project_dir=PROJ,
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
```

The 10 projects shipping with this convention as of April 2026:
`hgr10_life`, `hgr4_mandelbrot`, `hgr9_smiley16`, `hgr_orbital_pool`,
`tms9918_galaga`, `tms9918_life`, `tms9918_logo`, `tms9918_maze3d`,
`tms9918_orbital_pool`, `tms9918_snake`. Their pre-migration scripts
(60+ lines each) compressed to 23 lines apiece.

## Use from a project Makefile

```make
LOAD_CFG := ../../cc65/apple1_gen2.cfg

$(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
    ca65 $(LIB) $<
    ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@
```

If a project needs to *modify* one of these (different RAM size,
different code segment, different BSS layout), copy it into the
project folder and tweak there — keep this directory pristine for the
generic configs.
