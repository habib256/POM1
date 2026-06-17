# Build toutes les démos CodeTank + TMS (cc65) -> software/Apple-1_TMS_CC65/
DEMOS := hello_screen1 hello_world checksum graphs demo_screen1 picshow demo tetris text_adventure sprite_animals chrome_dino

# Card-neutral geometry/number layer shared with the GEN2 HGR card
# (dev/lib/gfx, factoring axis 1). `make gfx` builds the TMS9918 backend archive
# gfx-tms.lib; demos that link it (e.g. demos/demo) build it on demand via their
# own dependency, so this is just a convenience entry point. The archive is
# link-on-demand: until screen2.c forwards to gfx_*, it contributes 0 bytes.
GFXDIR := ../lib/gfx

.PHONY: all clean gfx $(DEMOS)

all: $(DEMOS)

gfx:
	$(MAKE) -C $(GFXDIR) tms

$(DEMOS):
	$(MAKE) -C demos/$@

clean:
	@for d in $(DEMOS); do $(MAKE) -C demos/$$d clean; done
	@$(MAKE) -C $(GFXDIR) clean
