# cc65/ — shared cc65 linker configs

Linker configs reusable by multiple projects. Project-specific configs (most
of them, since cartridge layouts differ across CodeTank banks, Sokoban variants,
etc.) live with their projects under `dev/projects/<name>/`.

## Files

- **`apple1.cfg`** — stock Apple-1, code at `$0300`, RAM up to `$8000`.
- **`apple1_4k.cfg`** — 4 KB-only variant (no RAM expansion). For Sokoban-4K
  and other tight-memory programs.
- **`apple1_gen2.cfg`** — Apple-1 + Uncle Bernie's GEN2 HGR card. Reserves
  `$2000-$3FFF` as the framebuffer; code lands above.
- **`pom1.cfg`** — POM1-specific, full 64 KB.

## Use from a project Makefile

    LOAD_CFG := ../../cc65/apple1_gen2.cfg

    $(OUT_DIR)/$(PROJECT).bin: $(PROJECT).asm
        ca65 $(LIB) $<
        ld65 -C $(LOAD_CFG) $(PROJECT).o -o $@

If a project needs to *modify* one of these (different RAM size, different
code segment, etc.), copy it into the project folder and tweak there — keep
this directory pristine.
