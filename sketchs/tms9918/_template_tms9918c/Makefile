# _template_tms9918c — minimal TMS9918 sprite (C / cc65) starter.
#
# Build:    make
# Clean:    make clean
# Load:     POM1 preset 9 (TMS9918 CodeTank), Load Memory > main.bin, then 4000R

PROJECT  := main
LIBDIR   := ../../../dev/lib
TMS9918C := $(LIBDIR)/tms9918c
CFG      := $(TMS9918C)/cc65/codetank_c.cfg

include $(TMS9918C)/tms9918c.mk    # TMS9918C_*_SRCS / TMS9918C_INCS

CL65   := cl65
CFLAGS := -t none -Oirs

# Only the families we actually call. ld65 dead-strips at the .o granularity,
# so omitting SCREEN2 / VSYNC / PRINTLIB / RANDOM / INTERRUPT saves real
# bytes vs the TMS9918C_ALL_SRCS line below.
SRCS := $(PROJECT).c \
        $(TMS9918C_CORE_SRCS) $(TMS9918C_SCREEN1_SRCS) \
        $(TMS9918C_SPRITES_SRCS) $(TMS9918C_APPLE1_SRCS)

# Need the full TMS9918 runtime instead? Swap the SRCS line above for:
#     SRCS := $(PROJECT).c $(TMS9918C_ALL_SRCS)

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(PROJECT).bin

$(PROJECT).bin: $(PROJECT).c $(TMS9918C)/tms9918c.h $(CFG)
	$(CL65) $(CFLAGS) -C $(CFG) $(TMS9918C_INCS) $(SRCS) -o $(PROJECT).bin
	@ls -l $(PROJECT).bin

clean:
	rm -f $(PROJECT).bin *.o
