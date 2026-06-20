# dev/lib/Makefile — self-validation gate for the 6502 library tracks.
#
#   make -C dev/lib check     equates cross-check + zp.inc layout pin +
#                             compile every library source to .o
#
# WHY THIS EXISTS. The libraries here are validated only INDIRECTLY today: a
# primitive is exercised when some project under dev/projects/ links it, and
# ld65 dead-strips (per-.o) whatever a project does NOT call. So a freshly added
# gfx_*/gen2_*/screen2_* helper that no shipping project references yet can rot
# (stop compiling) without any build going red. This gate compiles every source
# in the C runtimes' own .mk source sets — DECOUPLED from the consumers — plus
# the standalone asm objects, so coverage no longer depends on call sites.
#
# It also runs the two drift checks that previously lived as scripts nobody
# invoked automatically (fonts) or that did not exist (equates):
#   - tools/check_lib_equates.py    asm .inc  <->  C .h/.c hardware addresses
#   - tools/build_shared_font.py    one font master -> per-card tables
#
# Release: `make -C dev/lib check` AND `make -C dev/projects` must both be green.
# Requires cc65 (ca65/cl65) on PATH and python3.

REPO   := $(abspath ../..)
CL65   ?= cl65
CA65   ?= ca65
PYTHON ?= python3

# Matches every other cc65 build in the repo (see dev/lib/gfx/Makefile):
# -t none (no target runtime), -c (compile/assemble only), -Oirs. cl65 -c
# dispatches .c -> compile+assemble and .s -> assemble, so one rule covers both.
CFLAGS := -t none -c -Oirs
TMP    := .check_obj.o          # throwaway — we only care about exit status

# --- authoritative C source sets (pull the lists from the libs' own .mk) -----
APPLE1C  := apple1c
GEN2C    := gen2c
TMS9918C := tms9918c
include apple1c/apple1c.mk
include gen2c/gen2c.mk
include tms9918c/tms9918c.mk

# Per-runtime include paths for a -c compile (gfx.h is needed by the geometry
# forwarders in gen2c/tms9918c; apple1c.h by gen2.h).
APPLE1C_INC  := -I apple1c
GEN2C_INC    := -I gen2c -I apple1c -I gfx
TMS9918C_INC := -I tms9918c -I gfx

# --- standalone asm objects (projects link these as .o; the .include-style
# fragments are NOT standalone TUs and are covered via their consumers). -------
ASM_SRCS := tms9918/tms9918m1.asm tms9918/tms9918m2.asm m6502/math.asm
ASM_INC  := -I tms9918 -I apple1 -I m6502

.PHONY: check equates zp-test compile compile-c compile-gfx compile-asm clean

check: equates zp-test compile
	@echo "dev/lib: check OK -- equates, zp.inc layout, and all sources compile"

equates:
	@echo "[lib] hardware-equate cross-check (asm .inc <-> C) ..."
	@$(PYTHON) $(REPO)/tools/check_lib_equates.py
	@echo "[lib] shared-font drift check ..."
	@$(PYTHON) $(REPO)/tools/build_shared_font.py --check

zp-test:
	@echo "[lib] zp.inc slot-pool layout pin ..."
	@$(MAKE) --no-print-directory -C apple1/test test

compile: compile-c compile-gfx compile-asm
	@rm -f $(TMP)

compile-c:
	@echo "[lib] compiling C runtimes to .o (decoupled from projects) ..."
	@for s in $(APPLE1C_SRCS); do \
	  $(CL65) $(CFLAGS) $(APPLE1C_INC) -o $(TMP) $$s || { echo "  FAIL $$s"; exit 1; }; \
	  echo "  ok  $$s"; done
	@for s in $(GEN2C_ALL_SRCS); do \
	  $(CL65) $(CFLAGS) $(GEN2C_INC) -o $(TMP) $$s || { echo "  FAIL $$s"; exit 1; }; \
	  echo "  ok  $$s"; done
	@for s in $(TMS9918C_ALL_SRCS); do \
	  $(CL65) $(CFLAGS) $(TMS9918C_INC) -o $(TMP) $$s || { echo "  FAIL $$s"; exit 1; }; \
	  echo "  ok  $$s"; done

# gfx has its own archive build whose `check` compiles every shared TU against
# BOTH card backends — reuse it rather than duplicate the rules.
compile-gfx:
	@echo "[lib] gfx card-neutral layer (both backends) ..."
	@$(MAKE) --no-print-directory -C gfx check

compile-asm:
	@echo "[lib] assembling standalone asm objects to .o ..."
	@for s in $(ASM_SRCS); do \
	  $(CA65) $(ASM_INC) -o $(TMP) $$s || { echo "  FAIL $$s"; exit 1; }; \
	  echo "  ok  $$s"; done

clean:
	@rm -f $(TMP)
	@$(MAKE) --no-print-directory -C apple1/test clean
	@$(MAKE) --no-print-directory -C gfx clean
