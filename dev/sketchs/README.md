# DevBench Sketchs

Editable starter files for the POM1 DevBench.

The DevBench *New* dialog still uses the embedded starters in `src/Pom1BenchHost.cpp`.
These copies are kept here as regular files so they can be opened, edited, tested,
and used as a base for new sketches.

Folders are organized by DevBench machine profile first, then by source type. When
opened from DevBench, the path and extension select the matching environment:

- `apple1/asm/` - Apple-1 text assembly starters.
- `apple1/c/` - Apple-1 text C starters.
- `apple1/hex/` - Wozmon hex snippets.
- `apple1/raw/` - raw byte snippets loaded with the DevBench address field.
- `tms9918/asm/` - P-LAB TMS9918 assembly starters.
- `tms9918/c/` - P-LAB TMS9918 C starters.
- `gen2/asm/` - Uncle Bernie GEN2 HGR assembly starters.
- `gen2/c/` - Uncle Bernie GEN2 HGR C starters.

Detection rules:

- `.c` selects a C target; `.s` and `.asm` select an assembly target.
- A path containing `tms9918`, `tms9918c` or `codetank` selects the TMS9918 profile.
- A path containing `gen2`, `gen2c` or `hgr` selects the GEN2 profile.
- Anything else defaults to the Apple-1 text profile.

