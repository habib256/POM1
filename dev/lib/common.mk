# dev/lib/common.mk -- shared cc65 build convention for the 6502 library tracks.
#
# The SINGLE source of truth for the toolchain names + compile flags every cc65
# build under dev/lib uses. Previously copy-pasted into dev/lib/Makefile and
# dev/lib/gfx/Makefile with a "matches every other cc65 build" comment and no
# common definition -- so a flag change meant editing several files. Include
# this instead:  `include common.mk`  (root)  /  `include ../common.mk`  (gfx).
#
# All assignments are `?=` so a caller can still override on the command line
# (e.g. `make CL65=/opt/cc65/bin/cl65`) or from a parent make.

CL65 ?= cl65
CA65 ?= ca65
AR65 ?= ar65

# -t none : no target runtime (bare 6502, our own linker cfgs).
# -c      : compile/assemble only -- cl65 dispatches .c -> compile+assemble and
#           .s/.asm -> assemble, so one flag set covers both source kinds.
# -Oirs   : cc65 optimiser (inline, range, registers) -- the repo-wide default.
#
# cl65 QUIRK: -o is only honoured when it PRECEDES the input file. Always write
# `-o $@ $<`, never `$< -o $@`, in rules that use these flags.
CFLAGS ?= -t none -c -Oirs
