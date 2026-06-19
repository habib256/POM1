# dev/ — 6502 libraries, projects & build tree

Source code and Makefiles for Apple-1 programs. Developer guides live under
[`sketchs/doc/`](../sketchs/doc/) — start with [`APPLE1DEV.md`](../sketchs/doc/APPLE1DEV.md).

| Path | What |
|---|---|
| [`lib/`](lib/) | Shared cc65 libraries (`apple1`, `gen2`, `tms9918`, `gen2c`, …) |
| [`projects/`](projects/) | Complex multi-file projects (CI gate: `make -C dev/projects`) |
| [`cc65/`](cc65/) | Linker configs, shared Makefile fragments, emit scripts |
| [`../sketchs/`](../sketchs/) | DevBench sketches (mono-source programs) — see [`doc/SKETCHS.md`](../doc/SKETCHS.md) |
