# Makefile — build the card-neutral gfx layer (axis 1 of the GEN2/TMS9918
# graphics-library factoring; see README.md and dev/TODO6502.md).
#
# Output = two cc65 LIBRARY ARCHIVES (ar65), one per video card:
#
#   gfx-gen2.lib = gfx_draw + gfx_num + gfx_backend_gen2   (GEN2 HGR,   280x192)
#   gfx-tms.lib  = gfx_draw + gfx_num + gfx_backend_tms    (TMS9918 bm, 256x192)
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
gfx_draw.o: gfx_draw.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<
gfx_num.o: gfx_num.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) -o $@ $<

# --- per-card backends (need the card's header) ---
gfx_backend_gen2.o: gfx_backend_gen2.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(GEN2INC) -o $@ $<
gfx_backend_tms.o: gfx_backend_tms.c gfx.h
	$(CL65) $(CFLAGS) $(GFXINC) $(TMSINC) -o $@ $<

# --- archives (rm first so ar65 a never accumulates stale members) ---
gfx-gen2.lib: gfx_draw.o gfx_num.o gfx_backend_gen2.o
	rm -f $@
	$(AR65) a $@ $^
gfx-tms.lib: gfx_draw.o gfx_num.o gfx_backend_tms.o
	rm -f $@
	$(AR65) a $@ $^

# Compile-verification: building both archives compiles every shared TU against
# both backends (the blocking "compiler-vérifier les TUs partagées" step). The
# module dump confirms the expected exports are present.
check: gfx-gen2.lib gfx-tms.lib
	@echo "== gfx-gen2.lib modules =="; $(AR65) t gfx-gen2.lib
	@echo "== gfx-tms.lib  modules =="; $(AR65) t gfx-tms.lib
	@echo "[gfx] compile-verify OK (both backends)."

clean:
	rm -f *.o *.s *.lib
