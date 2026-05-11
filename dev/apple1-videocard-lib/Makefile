# Build toutes les démos CodeTank + TMS (cc65) -> software/Apple-1_TMS_CC65/
DEMOS := hello_screen1 hello_world checksum graphs demo_screen1 picshow demo tetris text_adventure sprite_animals chrome_dino

.PHONY: all clean $(DEMOS)

all: $(DEMOS)

$(DEMOS):
	$(MAKE) -C demos/$@

clean:
	@for d in $(DEMOS); do $(MAKE) -C demos/$$d clean; done
