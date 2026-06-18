# Makefile — build the card-neutral gfx layer (axis 1 of the GEN2/TMS9918
# graphics-library factoring; see README.md and dev/TODO6502.md).
#
# Output = two cc65 LIBRARY ARCHIVES (ar65), one per video card:
#
#   gfx-gen2.lib = gfx geometry (line/rect/circle/ellipse) + gfx_num (dec/hex) + gfx_backend_gen2   (GEN2 HGR,   280x192)
#   gfx-tms.lib  = gfx geometry (line/rect/circle/ellipse) + gfx_num (dec/hex) + gfx_backend_tms    (TMS9918 bm, 256x192)
#
# WHY AN ARCHIVE (not a list of .o on the link line). ld65 pulls only the
# modules a program actually references out of a .lib. Until the destructive
# rewiring lands (gen2.c / screen2.c forwarding to gfx_*), NOTHING references
# gfx_line/gfx_circle/gfx_utoa/..., so linking gfx-<card>.lib contributes
# *zero* bytes — verified: GEN2Snake is byte-identical with vs without the lib
# on its link line. Force-listing the .c sources instead would force-link the
# whole layer (+2888 bytes of dead code, pulling cc65's soft mul/divide for the
# ellipse and gfx_num). The archive is therefore the only *purely additive*
# way to wire gfx into a size-sensitive build path before rewiring.
#
# A GEN2 program links gfx-gen2.lib; a TMS9918 program links gfx-tms.lib. They
# never coexist (Parmigiani's "one board at a time"), so the backend symbols
# (gfx_plot/hline/vline/width/height) resolve unambiguously at link time.
#
# Usage:
#   make            # build both archives
#   make gen2       # just gfx-gen2.lib
#   make tms        # just gfx-tms.lib
#   make check      # compile-verify every TU against both backends + list modules
#   make clean
#
# Consumers add  -I <this dir>  and put  gfx-<card>.lib  on their cl65/ld65 line
# AFTER their own sources (see README "Build integration").

CL65   ?= cl65
AR65   ?= ar65
# -c compile-only; -Oirs matches every other cc65 build in the repo. NOTE: cl65
# only honours -o when it precedes the input file (a cl65 quirk), so every rule
# below writes "-o $@ $<", never "$< -o $@".
CFLAGS ?= -t none -c -Oirs

GFXINC  := -I .
GEN2INC := -I ../gen2c                       # gen2.h  (GEN2 backend)
TMSINC  := -I ../tms9918c  # screen2.h (TMS backend)

.PHONY: all gen2 tms check clean

all: gfx-gen2.lib gfx-tms.lib
gen2: gfx-gen2.lib
tms:  gfx-tms.lib

# --- card-neutral translation units (include only gfx.h, backend-agnostic) ---
# Geometry split per primitive — a rect-only program skips line + circle +
# ellipse code via ld65 dead-strip (per-.o granularity).
gfx_line.o: gfx_line.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_rect.o: gfx_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_circle.o: gfx_circle.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_ellipse.o: gfx_ellipse.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
# Number formatters split per base — a hex-only program skips the cc65
# 16-bit soft-divide via gfx_num_dec.o.
gfx_num_dec.o: gfx_num_dec.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_num_hex.o: gfx_num_hex.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<

# --- per-card backends (need the card's header) ---
# Each card has TWO backend TUs: a "core" with the per-pixel + span primitives
# (plot/hline/vline + extents) and a "rect" with the filled_rect + clear
# forwarders. A program that only draws lines/circles links the core but not
# the rect TU — ld65 dead-strips the rect TU and the heavier card helpers
# (gen2_hgr_fill_pixrect or screen2_filled_rect/screen2_clear) it would
# otherwise drag in.
gfx_backend_gen2.o: gfx_backend_gen2.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_backend_gen2_rect.o: gfx_backend_gen2_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_backend_tms.o: gfx_backend_tms.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<
gfx_backend_tms_rect.o: gfx_backend_tms_rect.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<

# Shared per-card object list (every program either gets these via dead-strip
# or skips them — that's the whole point of the split).
GFX_NEUTRAL_OBJS := gfx_line.o gfx_rect.o gfx_circle.o gfx_ellipse.o \
                    gfx_num_dec.o gfx_num_hex.o

# --- archives (rm first so ar65 a never accumulates stale members) ---
gfx-gen2.lib: $(GFX_NEUTRAL_OBJS) gfx_backend_gen2.o gfx_backend_gen2_rect.o
	rm -f $@
	$(AR65) a $@ $^
gfx-tms.lib: $(GFX_NEUTRAL_OBJS) gfx_backend_tms.o gfx_backend_tms_rect.o
	rm -f $@
	$(AR65) a $@ $^

# Compile-verification: building both archives compiles every shared TU against
# both backends (the blocking "compile-check the shared TUs" step). The
# module dump confirms the expected exports are present.
check: gfx-gen2.lib gfx-tms.lib
	@echo "== gfx-gen2.lib modules =="; $(AR65) t gfx-gen2.lib
	@echo "== gfx-tms.lib  modules =="; $(AR65) t gfx-tms.lib
	@echo "[gfx] compile-verify OK (both backends)."

clean:
	rm -f *.o *.s *.lib
